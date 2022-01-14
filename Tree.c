#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "HashMap.h"
#include "path_utils.h"

#include "Semaphore.h"
#include "NodeMonitor.h"

#include "Tree.h"

#ifndef PROTOCOL_DEBUG
	#define PROTOCOL_DEBUG 0
#endif

#include <sys/types.h>
#include <unistd.h>
#include <sys/syscall.h>

struct Tree {
	Tree * parent;
	Tree * newParent;  // For `move`.
	int inSubTree;     // Also for `move`.
	Semaphore * mutex; // For the protection of the above.
	Semaphore * removeSemaphore; // For safe tracebacks.
	bool isARemoveWaiting; // For safe tracebacks.
	HashMap * contents;
	NodeMonitor * monitor;
};

Tree * tree_new_node(Tree * parent) {
	Tree * result = (Tree *)malloc(sizeof(Tree));
	if (result == NULL) {
		return NULL;
	}

	result->parent = parent;
	result->newParent = NULL;
	result->inSubTree = 0;
	result->isARemoveWaiting = false;

	result->mutex = (Semaphore *)malloc(sizeof(Semaphore));
	result->removeSemaphore = (Semaphore *)malloc(sizeof(Semaphore));
	if (result->mutex == NULL || result->removeSemaphore == NULL ||
	    semInit(result->mutex, 1) != 0) {
		free(result->mutex);
		free(result->removeSemaphore);
		free(result);
		return NULL;
	}

	if (semInit(result->removeSemaphore, 0) != 0) {
		semDestroy(result->mutex);
		free(result->mutex);
		free(result->removeSemaphore);
		free(result);
		return NULL;
	}

	result->contents = hmap_new();
	if (result->contents == NULL) {
		semDestroy(result->removeSemaphore);
		semDestroy(result->mutex);
		free(result->removeSemaphore);
		free(result->mutex);
		free(result);
		return NULL;
	}

	result->monitor = (NodeMonitor *)malloc(sizeof(NodeMonitor));
	if (result->monitor == NULL || nmInit(result->monitor) != 0) {
		free(result->monitor);
		semDestroy(result->removeSemaphore);
		semDestroy(result->mutex);
		free(result->removeSemaphore);
		free(result->mutex);
		hmap_free(result->contents);
		return NULL;
	}

	return result;
}

Tree * tree_new() {
	return tree_new_node(NULL);
}

void tree_free(Tree * tree) {
	// Free resources not associated with the hashmap.
	nmDestroy(tree->monitor);
	free(tree->monitor);
	semDestroy(tree->mutex);
	free(tree->mutex);
	semDestroy(tree->removeSemaphore);
	free(tree->removeSemaphore);

	// Destroy all subtrees.
	const char * key;
	void * value;
	HashMapIterator it = hmap_iterator(tree->contents);
	while (hmap_next(tree->contents, &it, &key, &value)) {
		tree_free(value);
	}

	// Destroy the hashmap.
	hmap_free(tree->contents);
	// And the tree.
	free(tree);
}

// Starts at a node referenced by the pointer, assuming it has
// a read lock on it. Travels up the filesystem, reducing
// the `inSubTree` counters. Necessary for rollbacks.
// The `writeLock` argument indicates whether the function starts
// with a write lock or a read lock.
// Unlike `tree_find`, does not require obtaining locks on nodes,
// but must make sure that the nodes it is yet to access, are not removed.
// Traces back only up to the node pointed to by `upTo` and `including`
// indicates whether it should also include that node.
void tree_trace_back(Tree * tree, bool writeLock, Tree * upTo, bool including) {
	if (PROTOCOL_DEBUG) {
		fprintf(stderr, "Begin traceback at %p %d up to %p %d.\n", tree->monitor, writeLock, upTo->monitor, including);
	}
	if (tree == NULL) {
		return;
	}

	Tree * parent;

	// Update the inSubTree counter and parent pointer.
	semP(tree->mutex);
	parent = tree->parent;
	tree->inSubTree--;
	// If there was a move performed and the parent changed,
	// and no more threads are working in the subtree,
	// update the parent pointer and unlock entry protocols.
	if (tree->inSubTree == 0 && tree->newParent != NULL) {
		tree->parent = tree->newParent;
		tree->newParent = NULL;
		nmUnlock(tree->monitor);
	}
	semV(tree->mutex);
	// End of update.

	// Release the lock on the starting node.
	if (writeLock) {
		nmWriterExit(tree->monitor);
	} else {
		nmReaderExit(tree->monitor);
	}

	while ((including && tree != upTo) || (!including && parent != upTo)) {
		tree = parent;
		// Update the inSubTree counter and parent pointer.
		semP(tree->mutex);
		parent = tree->parent;
		tree->inSubTree--;
		// If there was a move performed and the parent changed,
		// and no more threads are working in the subtree,
		// update the parent pointer and unlock entry protocols.
		if (tree->inSubTree == 0 && tree->newParent != NULL) {
			tree->parent = tree->newParent;
			tree->newParent = NULL;
			nmUnlock(tree->monitor);
			semV(tree->mutex);
		// If a remove operation is waiting, let it remove the node,
		// now that it is safe for tracebacks.
		} else if (tree->inSubTree == 1 && tree->isARemoveWaiting) {
			semV(tree->removeSemaphore);
		} else {
			semV(tree->mutex);
		}
		// End of update.
	}

	if (PROTOCOL_DEBUG) {
		fprintf(stderr, "End of traceback.\n");
	}
}

// Finds the appropriate node by path in the filesystem structure, 
// returning the pointer to the target node on success, and NULL otherwise.
// Guarantees a lock on the target. A read lock if `writeLock` = false,
// and a write lock if `writeLock` = true. 
// This function sets errno to 0 on success, and to ENOENT if the path doesn't exist.
// Anything else means a system error, like a pthread function error.

Tree * tree_find(Tree * tree, const char * path, bool writeLock) {
	Tree * root = tree;
	if (tree == NULL || path == NULL) {
		return NULL;
	}

	Tree * child;
	char component[MAX_FOLDER_NAME_LENGTH + 1];

	while (!is_root_path(path)) {
		// Gain read access and release read access to parent.
		nmReaderEnter(tree->monitor);
		semP(tree->mutex);
		// This is a funny conditional statement.
		// If the parent is NULL, that is we are in "/", so we should skip freeing up the parent.
		// However, if the current vertex is the one we started tree_find in, then we mustn't
		// meddle with the protocols of its parents.
		if (tree->parent != NULL && tree != root) {
			nmReaderExit(tree->parent->monitor);
		}
		tree->inSubTree++;
		semV(tree->mutex);

		// Search for child.
		path = split_path(path, component);
		child = hmap_get(tree->contents, component);
		if (child == NULL) {
			// This is valid, we have a read lock.
			tree_trace_back(tree, false, root, true);
			errno = ENOENT;
			return NULL;
		} else {
			tree = child;
		}
	}

	// At this point, we have a read lock on the parent and a pointer
	// to the target node, which means it cannot be removed or moved.
	// It remains to gain a proper lock on the target,
	// release the lock on the parent, and return.

	if (writeLock) {
		nmWriterEnter(tree->monitor);
	} else {
		nmReaderEnter(tree->monitor);
	}

	semP(tree->mutex);
	if (tree->parent != NULL && tree != root) {
		nmReaderExit(tree->parent->monitor);
	}
	tree->inSubTree++;
	semV(tree->mutex);

	return tree;
}

// Similar to `tree_find`, but finds two DIFFERENT nodes and acquires ONLY WRITE locks on them.

void tree_find_two(Tree * tree, const char * path1, const char * path2, Tree * * resultLCA, Tree * * result1, Tree * * result2) {
	if (PROTOCOL_DEBUG) {
		fprintf(stderr, "Thread %ld: looking for %s and %s\n", syscall(__NR_gettid), path1, path2);
	}

	Tree * root = tree;
	char LCAPath[MAX_PATH_LENGTH + 1];
	char SuffixTab1[MAX_PATH_LENGTH + 1];
	char SuffixTab2[MAX_PATH_LENGTH + 1];
	char component1[MAX_FOLDER_NAME_LENGTH + 1];
	char component2[MAX_FOLDER_NAME_LENGTH + 1];
	char * Suffix1 = SuffixTab1, * Suffix2 = SuffixTab2;
	split_paths_by_LCA(path1, path2, LCAPath, Suffix1, Suffix2);
	*result1 = *result2 = NULL;
	if (resultLCA) {
		*resultLCA = NULL;
	}

	Tree * LCA, * lesser, * greater;
	Tree * lesserChild, * greaterChild;
	bool swappedOrder;

	if (is_lesser_path(path1, path2)) {
		swappedOrder = false;
	} else {
		swappedOrder = true;
		char * temp = Suffix1;
		Suffix1 = Suffix2;
		Suffix2 = temp;
	}

	// Find the LCA.
	bool isLCAEqualLesser = (is_root_path(Suffix1));
	if (isLCAEqualLesser) {
		LCA = tree_find(tree, LCAPath, true);
		lesser = LCA;
	} else {
		LCA = tree_find(tree, LCAPath, false);
	}
	if (LCA == NULL) {
		return;
	}

	// Find the lesser node (if not equal to LCA).
	if (!isLCAEqualLesser) {
		Suffix1 = (char *)split_path(Suffix1, component1);
		lesserChild = hmap_get(LCA->contents, component1);
		lesser = tree_find(lesserChild, Suffix1, true);
		if (lesser == NULL) {
			errno = ENOENT;
			tree_trace_back(LCA, false, root, true);
			return;
		}
	}

	// Find the greater node.
	Suffix2 = (char *)split_path(Suffix2, component2);
	greaterChild = hmap_get(LCA->contents, component2);
	greater = tree_find(greaterChild, Suffix2, true);
	if (greater == NULL) {
		errno = ENOENT;
		if (isLCAEqualLesser) {
			tree_trace_back(lesser, true, root, true);
		} else {
			tree_trace_back(lesser, true, LCA, false);
			tree_trace_back(LCA, false, root, true);
		}
		return;
	}

	// Unlock the LCA if necessary
	// (it only ever had a read lock if it wasn't one of the wanted nodes).
	// Then write the results to the result pointers.
	if (!isLCAEqualLesser) {
		nmReaderExit(LCA->monitor);
	}
	if (swappedOrder) {
		*result1 = greater;
		*result2 = lesser;
	} else {
		*result1 = lesser;
		*result2 = greater;
	}
	if (resultLCA) {
		*resultLCA = LCA;
	}

	if (PROTOCOL_DEBUG) {
		fprintf(stderr, "Thread %ld: found them!\n", syscall(__NR_gettid));
	}
}

char * tree_list(Tree * tree, const char * path) {
	Tree * root = tree;
	errno = 0;

	// Check path validity
	if (tree == NULL || !is_path_valid(path)) {
		errno = EINVAL;
		return NULL;
	}

	// Obtain a read lock on the target node.
	tree = tree_find(tree, path, false);
	if (tree == NULL) {
		return NULL;
	}

	// Create the contents string of the hashmap in the proper filesystem node
	char * result = make_map_contents_string(tree->contents);

	// Exit the tree structure.
	tree_trace_back(tree, false, root, true);

	// Return result;
	return result;
}

int tree_create(Tree * tree, const char * path) {
	Tree * root = tree;
	// fprintf(stderr, "\t\t\t\tstart tree_create: %s\n", path);

	errno = 0;

	// Check path validity
	if (tree == NULL || !is_path_valid(path)) {
		errno = EINVAL;
		return errno;
	} else if (is_root_path(path)) {
		errno = EEXIST;
		return errno;
	}

	char parentPath[MAX_PATH_LENGTH + 1];
	char component[MAX_FOLDER_NAME_LENGTH + 1];
	make_path_to_parent(path, parentPath, component);

	// Obtain a write lock on the parent of the target node.
	Tree * parent;
	parent = tree_find(tree, parentPath, true);
	if (parent == NULL) {
		return errno;
	}

	// Create the target node.
	Tree * target = tree_new_node(parent);
	if (target == NULL) {
		tree_trace_back(parent, true, root, true);
		return errno;
	}

	// Try inserting. If the node already exists, free memory and return error.
	if (!hmap_insert(parent->contents, component, target)) {
		tree_free(target);
		tree_trace_back(parent, true, root, true);
		errno = EEXIST;
		return errno;
	} else {
		tree_trace_back(parent, true, root, true);
	}

	// fprintf(stderr, "\t\t\t\tend tree_create: %s\n", path);

	return errno;
}

int tree_remove(Tree * tree, const char * path) {
	Tree * root = tree;
	// fprintf(stderr, "\t\t\t\tstart tree_remove: %s\n", path);

	errno = 0;

	// Check path validity
	if (tree == NULL || !is_path_valid(path)) {
		errno = EINVAL;
		return errno;
	} else if (is_root_path(path)) {
		errno = EBUSY;
		return errno;
	}

	char parentPath[MAX_PATH_LENGTH + 1];
	char component[MAX_FOLDER_NAME_LENGTH + 1];
	Tree * parent, * target;

	make_path_to_parent(path, parentPath, component);

	// Obtain write locks on both the target and the parent of the target.
	tree_find_two(tree, parentPath, path, NULL, &parent, &target);

	if (target == NULL) {
		errno = ENOENT;
		return errno;
	}

	// Now, `parent` is pointing to the node from which the given node needs to be removed,
	// and `target` points to the node to be removed. We must check if it's empty, then remove,
	// but only after there are no operations left to trace back through that node.
	if (hmap_size(target->contents) != 0) {
		tree_trace_back(target, true, target, true);
		tree_trace_back(parent, true, root, true);
		errno = ENOTEMPTY;
		return errno;
	}

	semP(target->mutex);
	if (target->inSubTree > 1) {
		target->isARemoveWaiting = true;
		semV(target->mutex);
		semP(target->removeSemaphore);
		target->isARemoveWaiting = false;
	}
	semV(target->mutex);

	hmap_remove(parent->contents, component);
	tree_free(target);
	tree_trace_back(parent, true, root, true);

	// fprintf(stderr, "\t\t\t\tend tree_remove: %s\n", path);

	return errno;
}

int tree_move(Tree * tree, const char * source, const char * target) {
	Tree * root = tree;
	// fprintf(stderr, "\t\t\t\tstart tree_move: %s -> %s\n", source, target);
	errno = 0;

	// Check path validity
	if (tree == NULL || !(is_path_valid(source) && is_path_valid(target))) {
		errno = EINVAL;
		return errno;
	} else if (is_root_path(source) || is_proper_prefix_of_path(source, target)) {
		errno = EBUSY;
		return errno;
	} else if (is_root_path(target)) {
		errno  = EEXIST;
		return errno;
	}

	char sourceParentPath[MAX_PATH_LENGTH + 1];
	char targetParentPath[MAX_PATH_LENGTH + 1];
	char sourceComponent[MAX_FOLDER_NAME_LENGTH + 1];
	char targetComponent[MAX_FOLDER_NAME_LENGTH + 1];

	make_path_to_parent(source, sourceParentPath, sourceComponent);
	make_path_to_parent(target, targetParentPath, targetComponent);

	bool sameParent = are_same_path(sourceParentPath, targetParentPath);

	Tree * sourceParent;
	Tree * targetParent;
	Tree * LCA;
	Tree * sourceTarget;
	Tree * targetTarget;

	if (sameParent) {
		sourceParent = targetParent = tree_find(tree, sourceParentPath, true);
	} else {
		tree_find_two(tree, sourceParentPath, targetParentPath, &LCA, &sourceParent, &targetParent);
	}

	if (sourceParent == NULL) {
		return errno;
	}

	// Woohoo, we have a nice and easy write lock on both parents.
	// ALL locks in ALL processes are obtained lexicographically
	// within the processes, so assuming a finite number of processes
	// (and we have that assumption in the project statement), there will
	// be no loss of liveness, no deadlocks, no nothing! 🎉

	// Obtain a pointer to the source target and try to obtain one for the target target.
	sourceTarget = hmap_get(sourceParent->contents, sourceComponent);
	targetTarget = hmap_get(targetParent->contents, targetComponent);

	if (sourceTarget == NULL) {
		errno = ENOENT;
	} else if (targetTarget != NULL) {
		errno = EEXIST;
	}

	if (errno != 0) {
		// It doesn't really matter in which order we free the locks,
		// as it does not depend on obtaining other locks.
		if (sameParent) {
			tree_trace_back(targetParent, true, root, true);
		} else {
			if (targetParent == LCA) {
				tree_trace_back(sourceParent, true, LCA, false);
				tree_trace_back(targetParent, true, root, true);
			} else {
				tree_trace_back(targetParent, true, LCA, false);
				tree_trace_back(sourceParent, true, root, true);
			}
		}
		return errno;
	}

	// All set and all logic conditions were met. Time for the actual move.

	// Obtain mutex metadata protection for the source node. 
	semP(sourceTarget->mutex);
	// Perform the actual move.
	hmap_remove(sourceParent->contents, sourceComponent);
	hmap_insert(targetParent->contents, targetComponent, sourceTarget);
	// Adjust metadata and lock the target if necessary.
	if (sourceTarget->inSubTree == 0) {
		// If there was no thread in the subtree, just swap the parent pointer.
		sourceTarget->parent = targetParent;
	} else {
		// Else, save the new parent pointer, and lock the entry protocols.
		sourceTarget->newParent = targetParent;
		nmLock(sourceTarget->monitor);
	}
	// Release the metadata protection.
	semV(sourceTarget->mutex);

	// Perform the tracebacks
	if (sameParent) {
		tree_trace_back(targetParent, true, root, true);
	} else {
		if (targetParent == LCA) {
				tree_trace_back(sourceParent, true, LCA, false);
				tree_trace_back(targetParent, true, root, true);
			} else {
				tree_trace_back(targetParent, true, LCA, false);
				tree_trace_back(sourceParent, true, root, true);
			}
	}

	return errno;
}