#include <stdio.h>
#include <errno.h>
#include <pthread.h>

#include <sys/types.h>
#include <unistd.h>
#include <sys/syscall.h>

#include "Semaphore.h"

#include "NodeMonitor.h"

/**
 * Here's how the protocols work:
 * 
 * 1) Lock requirements
 *   create: requires a write lock on the parent of the target.
 *   remove: requries a write lock on the parent and target.
 *     A write lock is required on the target because a node must not be removed
 *     when another thread is, for example, listing its contents, even if it has no contents.
 *     Also, no further locks are required (e.g. a lock on all the nodes in a subtree), because
 *     if they were required, then they would need to exist, at which point the remove operation
 *     must fail anyway with ENOTEMPTY
 *   list: requires a read lock on the target.
 *   find: requires a read lock on the parent of the (actual) target. In actuality, find will obtain
 *     a read lock on the target instead, because the parent of the target will be given
 *     as the target of the find operation. Additionally, the find operation requires a chain of
 *     read locks for all the ancestors of the target. For any given node, find will first have
 *     to obtain a read lock on the parent of the node, then it will receive a read lock on the
 *     node (if it exists), and lastly, it will release the read lock on the parent.
 *   move: requires a write lock on the parent of the source and the parent of the target.
 *     Requires also a write lock on the source to set proper flags and parent pointers.
 * 
 * 2) The problem
 *   The only problem with a standard approach, assuming the above locks, would be the appearance of
 *   race conditions between two threads, one working on a folder in a subtree before a move, the other
 *   working on the same subtree, but after a move, resulting in a different path. Were the second thread
 *   to finish its job before the first, weird things would happen in all kinds of scenarios.
 * 
 * 3) The solution
 *   For each subtree, we remember how many threads are doing something inside it. Then, if a subtree
 *   gets moved, we do not let in any new threads inside until all previous threads have exited.
 *   This means we disable entry protocols, but keep the exit protocols enabled.
 *   This means that it will be necessary to backtrack up the filesystem structure, to decrement
 *   the thread counters. This also means, that we need to keep track of the parent of a node
 *   (not here, in `struct Tree`), and additionally, the new parent (brought to you by yours truly, tree_move).
 * 
 * 4) Addendum: guarantee of liveness
 *   Each operation obtains locks lexicographically. Assuming a finite amount of threads (given by the project statment)
 *   this guarantees liveness if properly done (and there was trouble with the last part).
 *   This means that operations which require two write locks, particularly the move operation
 *   must separately find the LCA in order to not look for it again, as looking for it again, either
 *   by tracing back up the structure or by going down from the root would contradict the condition
 *   that locks are to be taken lexicographically.
 */

/**
 * The protocols are mostly copied from the provided .pdf on Moodle.
 * The protocols make use of critical section inheritance.
 */

int nmInit(NodeMonitor * nm) {
	if (nm == NULL) {
		return 0;
	}

	int err;
	nm->reading = nm->writing = nm->waitingR = nm->waitingW = 0; // fite me
	if ((err = semInit(&nm->mutex, 1)) != 0) {
		errno = err;
		return errno;
	}
	if ((err = semInit(&nm->entryMutex, 1)) != 0) {
		// Ignore possible errors.
		semDestroy(&nm->mutex);
		errno = err;
		return errno;
	}
	if ((err = semInit(&nm->readers, 0)) != 0) {
		semDestroy(&nm->mutex);
		semDestroy(&nm->entryMutex);
		errno = err;
		return errno;
	}
	if ((err = semInit(&nm->writers, 0)) != 0) {
		semDestroy(&nm->mutex);
		semDestroy(&nm->entryMutex);
		semDestroy(&nm->readers);
		// I think I understand the appeal of RAII.
		errno = err;
		return errno;
	}

	return 0;
}

int nmDestroy(NodeMonitor * nm) {
	if (nm == NULL) {
		return 0;
	}

	int err;
	if ((err = semDestroy(&nm->mutex)) != 0) {
		// Ignore possible errors.
		semDestroy(&nm->entryMutex);
		semDestroy(&nm->readers);
		semDestroy(&nm->writers);
		errno = err;
		return errno;
	}
	if ((err = semDestroy(&nm->entryMutex)) != 0) {
		semDestroy(&nm->readers);
		semDestroy(&nm->writers);
		errno = err;
		return errno;
	}
	if ((err = semDestroy(&nm->readers)) != 0) {
		semDestroy(&nm->writers);
		errno = err;
		return errno;
	}
	if ((err = semDestroy(&nm->writers)) != 0) {
		errno = err;
		return errno;
	}

	return 0;
}

void nmReaderEnter(NodeMonitor * nm) {
	semP(&nm->entryMutex);
	semP(&nm->mutex);
	semV(&nm->entryMutex);
	if (PROTOCOL_DEBUG != 0) {
		fprintf(stderr, "Thread %ld: Reader Entry at %p.\n%d, %d, %d, %d\n\n", syscall(__NR_gettid), nm, nm->reading, nm->writing, nm->waitingR, nm->waitingW);
	}
	if (nm->writing + nm->waitingW > 0) {
		nm->waitingR++;
		semV(&nm->mutex);
		semP(&nm->readers);
		nm->waitingR--;
	}
	nm->reading++;
	if (nm->waitingR > 0) {
		semV(&nm->readers);
	} else {
		semV(&nm->mutex);
	}
}

void nmReaderExit(NodeMonitor * nm) {
	semP(&nm->mutex);
	if (PROTOCOL_DEBUG != 0) {
		fprintf(stderr, "Thread %ld: Reader Exit at %p.\n%d, %d, %d, %d\n\n", syscall(__NR_gettid), nm, nm->reading, nm->writing, nm->waitingR, nm->waitingW);
	}
	nm->reading--;
	if (nm->reading == 0 && nm->waitingW > 0) {
		semV(&nm->writers);
	} else {
		semV(&nm->mutex);
	}
}

void nmWriterEnter(NodeMonitor * nm) {
	semP(&nm->entryMutex);
	semP(&nm->mutex);
	semV(&nm->entryMutex);
	if (PROTOCOL_DEBUG != 0) {
		fprintf(stderr, "Thread %ld: Writer Entry at %p.\n%d, %d, %d, %d\n\n", syscall(__NR_gettid), nm, nm->reading, nm->writing, nm->waitingR, nm->waitingW);
	}
	if (nm->reading + nm->writing > 0) {
		nm->waitingW++;
		semV(&nm->mutex);
		semP(&nm->writers);
		nm->waitingW--;
	}
	nm->writing++;
	semV(&nm->mutex);
}

void nmWriterExit(NodeMonitor * nm) {
	semP(&nm->mutex);
	if (PROTOCOL_DEBUG != 0) {
		fprintf(stderr, "Thread %ld: Writer Exit at %p.\n%d, %d, %d, %d\n\n", syscall(__NR_gettid), nm, nm->reading, nm->writing, nm->waitingR, nm->waitingW);
	}
	nm->writing--;
	if (nm->waitingR > 0) {
		semV(&nm->readers);
	} else if (nm->waitingW > 0) {
		semV(&nm->writers);
	} else {
		semV(&nm->mutex);
	}
}

void nmLock(NodeMonitor * nm) {
	if (PROTOCOL_DEBUG != 0) {
		fprintf(stderr, "Thread %ld: Lock at %p.\n%d, %d, %d, %d\n\n", syscall(__NR_gettid), nm, nm->reading, nm->writing, nm->waitingR, nm->waitingW);
	}
	semP(&nm->entryMutex);
}

void nmUnlock(NodeMonitor * nm) {
	if (PROTOCOL_DEBUG != 0) {
		fprintf(stderr, "Thread %ld: Unlock at %p.\n%d, %d, %d, %d\n\n", syscall(__NR_gettid), nm, nm->reading, nm->writing, nm->waitingR, nm->waitingW);
	}
	semV(&nm->entryMutex);
}