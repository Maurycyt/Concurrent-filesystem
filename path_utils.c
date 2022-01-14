#include "path_utils.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * File kindly provided by the university,
 * with certain modifications from myself,
 * and certain functions, particularly
 * split_paths_by_LCA added by myself.
 */

bool is_path_valid(const char* path)
{
    if (path == NULL)
        return false;
    size_t len = strlen(path);
    if (len == 0 || len > MAX_PATH_LENGTH)
        return false;
    if (path[0] != '/' || path[len - 1] != '/')
        return false;
    const char* name_start = path + 1; // Start of current path component, just after '/'.
    while (name_start < path + len) {
        char* name_end = strchr(name_start, '/'); // End of current path component, at '/'.
        if (!name_end || name_end == name_start || name_end > name_start + MAX_FOLDER_NAME_LENGTH)
            return false;
        for (const char* p = name_start; p != name_end; ++p)
            if (*p < 'a' || *p > 'z')
                return false;
        name_start = name_end + 1;
    }
    return true;
}

bool is_root_path(const char* path)
{
    return path[0] == '/' && path[1] == '\0';
}

bool are_same_path(const char* path1, const char* path2)
{
    if (path1 == path2) { // Including NULL!
        return true;
    } else if (path1 == NULL || path2 == NULL) {
        return false;
    } else {
        return strcmp(path1, path2) == 0;
    }
}

bool is_lesser_path(const char* path1, const char* path2)
{
    if (path1 == path2) { // Including NULL!
        return false;
    } else if (path1 == NULL || path2 == NULL) {
        return false;
    } else {
        return strcmp(path1, path2) < 0;
    }	
}

const char* split_path(const char* path, char* component)
{
    const char* subpath = strchr(path + 1, '/'); // Pointer to second '/' character.
    if (!subpath) // Path is "/".
        return NULL;
    if (component) {
        int len = subpath - (path + 1);
        assert(len >= 1 && len <= MAX_FOLDER_NAME_LENGTH);
        strncpy(component, path + 1, len);
        component[len] = '\0';
    }
    return subpath;
}

void make_path_to_parent(const char* path, char* result, char* component)
{
    size_t len = strlen(path);
    if (len == 1) // Path is "/".
        return;
    const char* p = path + len - 2; // Point before final '/' character.
    // Move p to last-but-one '/' character.
    while (*p != '/')
        p--;

    size_t subpath_len = p - path + 1; // Include '/' at p.
    strncpy(result, path, subpath_len);
    result[subpath_len] = '\0';

    if (component) {
        size_t component_len = len - subpath_len - 1; // Skip final '/' as well.
        assert(component_len >= 1 && component_len <= MAX_FOLDER_NAME_LENGTH);
        strncpy(component, p + 1, component_len);
        component[component_len] = '\0';
    }
}

void split_paths_by_LCA(const char* path1, const char* path2, char* LCA, char* suffix1, char* suffix2) {
    int lastSlash = 0;
    int pos = 1;
    while (path1[pos] != '\0' && path2[pos] != '\0') {
        if (path1[pos] == path2[pos]) {
            if (path1[pos] == '/') {
                lastSlash = pos;
            }
            pos++;
        } else {
            break;
        }
    }

    for (int i = 0; i <= lastSlash; i++) {
        LCA[i] = path1[i];
    }
    LCA[lastSlash + 1] = '\0';

    int len1 = strlen(path1);
    for (int i = lastSlash; i <= len1; i++) {
        suffix1[i - lastSlash] = path1[i];
    }

    int len2 = strlen(path2);
    for (int i = lastSlash; i <= len2; i++) {
        suffix2[i - lastSlash] = path2[i];
    }
}

void get_last_path_component(const char* path, char* component)
{
    size_t len = strlen(path);
    if (len == 1) { // Path is "/".
        component[0] = '\0';
        return;
    }

    const char* p = path + len - 2; // Point before final '/' character.
    size_t component_len = 0;
    // Move p to last-but-one '/' character.
    while (*p != '/') {
        p--;
        component_len++;
    }
    
    if (component) {
        assert(component_len >= 1 && component_len <= MAX_FOLDER_NAME_LENGTH);
        strncpy(component, p + 1, component_len);
        component[component_len] = '\0';
    }
}

bool is_proper_prefix_of_path(const char* prefix, const char* path)
{
    size_t prefix_len = strlen(prefix);
    size_t path_len = strlen(path);

    if (prefix_len >= path_len) {
    	return false;
    }

    bool result = true;
    for (size_t i = 0; i < prefix_len; i++) {
        result &= prefix[i] == path[i];
    }

    return result;
}

// A wrapper for using strcmp in qsort.
// The arguments here are actually pointers to (const char*).
static int compare_string_pointers(const void* p1, const void* p2)
{
    return strcmp(*(const char**)p1, *(const char**)p2);
}

const char** make_map_contents_array(HashMap* map)
{
    size_t n_keys = hmap_size(map);
    const char** result = calloc(n_keys + 1, sizeof(char*));
    HashMapIterator it = hmap_iterator(map);
    const char** key = result;
    void* value = NULL;
    while (hmap_next(map, &it, key, &value)) {
        key++;
    }
    *key = NULL; // Set last array element to NULL.
    qsort(result, n_keys, sizeof(char*), compare_string_pointers);
    return result;
}

char* make_map_contents_string(HashMap* map)
{
    const char** keys = make_map_contents_array(map);

    unsigned int result_size = 0; // Including ending null character.
    for (const char** key = keys; *key; ++key)
        result_size += strlen(*key) + 1;

    // Return empty string if map is empty.
    if (!result_size) {
        // Note we can't just return "", as it can't be free'd.
        char* result = malloc(1);
        *result = '\0';
        free(keys);
        return result;
    }

    char* result = malloc(result_size);
    char* position = result;
    for (const char** key = keys; *key; ++key) {
        size_t keylen = strlen(*key);
        assert(position + keylen <= result + result_size);
        strcpy(position, *key); // NOLINT: array size already checked.
        position += keylen;
        *position = ',';
        position++;
    }
    position--;
    *position = '\0';
    free(keys);
    return result;
}
