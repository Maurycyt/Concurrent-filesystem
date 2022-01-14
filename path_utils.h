#pragma once

#include <stdbool.h>

#include "HashMap.h"

/**
 * File kindly provided by the university,
 * with certain modifications from myself,
 * and certain functions, particularly
 * split_paths_by_LCA added by myself.
 */

// Max length of path (excluding terminating null character).
#define MAX_PATH_LENGTH 4095

// Max length of folder name (excluding terminating null character).
#define MAX_FOLDER_NAME_LENGTH 255

// Return whether a path is valid.
// Valid paths are '/'-separated sequences of folder names, always starting and ending with '/'.
// Valid paths have length at most MAX_PATH_LENGTH (and at least 1). Valid folder names are are
// sequences of 'a'-'z' ASCII characters, of length from 1 to MAX_FOLDER_NAME_LENGTH.
bool is_path_valid(const char* path);

// Returns whether a path is "/".
bool is_root_path(const char* path);

// Returns whether two paths are the same.
bool are_same_path(const char* path1, const char* path2);

// Returns whether the first path is lesser lexicographically than the other.
bool is_lesser_path(const char* path1, const char* path2);

// Return the subpath obtained by removing the first component.
// Args:
// - `path`: should be a valid path (see `is_path_valid`).
// - `component`: if not NULL, should be a buffer of size at least MAX_FOLDER_NAME_LENGTH + 1.
//    Then the first component will be copied there (without any '/' characters).
// If path is "/", returns NULL and leaves `component` unchanged.
// Otherwise the returns a pointer into `path`, representing a valid subpath.
//
// This can be used to iterate over all components of a path:
//     char component[MAX_FOLDER_NAME_LENGTH + 1];
//     const char* subpath = path;
//     while (subpath = split_path(subpath, component))
//         printf("%s", component);
const char* split_path(const char* path, char* component);

// Return a copy of the subpath obtained by removing the last component.
// The caller should free the result, unless it is NULL.
// Args:
// - `path`: should be a valid path (see `is_path_valid`).
// - `component`: if not NULL, should be a buffer of size at least MAX_FOLDER_NAME_LENGTH + 1.
//    Then the last component will be copied there (without any '/' characters).
// If path is "/", returns NULL and leaves `component` unchanged.
// Otherwise the result is a valid path.
void make_path_to_parent(const char* path, char* result, char* component);

// Finds the LCA of two valid paths, then saves it to LCA,
// and saves the leftovers into suffix1 and suffix2,
// all three shoul be buffers of appropriate length.
void split_paths_by_LCA(const char* path1, const char* path2, char* LCA, char* suffix1, char* suffix2);

/*
// Copies the last component of a path into `component`.
// In effect, works exactly like `make_path_to_parent` but
// does not return a subpath to the parent.
void get_last_path_component(const char* path, char* component);
*/

// Returns whether `prefix` is a prefix of `path` and isn't equal to `path`.
bool is_proper_prefix_of_path(const char* prefix, const char* path);

// Return an array containing all keys, lexicographically sorted.
// The result is null-terminated.
// Keys are not copied, they are only valid as long as the map.
// The caller should free the result.
const char** make_map_contents_array(HashMap* map);

// Return a string containing all keys in map, sorted, comma-separated.
// The result has no trailing comma. An empty map yields an empty string.
// The caller should free the result.
char* make_map_contents_string(HashMap* map);
