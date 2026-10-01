/**
 * Copyright, Philip Meulengracht
 *
 * This program is free software : you can redistribute it and / or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation ? , either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 */

// windowsfilter layerchain.json handling. A layer folder's layerchain.json lists its
// parent layer folders (top-most first); HCS and wclayer need the fully expanded chain.

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <shlwapi.h>
#include <jansson.h>
#include <vlog.h>

#include "private.h"

static int __layerchain_path(const char* layer_dir, char* buffer, size_t length)
{
    int rc;

    if (layer_dir == NULL || layer_dir[0] == '\0') {
        return -1;
    }

    rc = snprintf(buffer, length, "%s\\layerchain.json", layer_dir);
    return (rc < 0 || (size_t)rc >= length) ? -1 : 0;
}

// Resolve a layerchain entry to an existing directory. Entries may be absolute, relative to
// the layer folder, or (Docker layout) only resolvable as <layer>\parents\<basename>.
static char* __layerchain_resolve_entry(const char* layer_dir, const char* entry)
{
    char        resolved[MAX_PATH];
    const char* base;
    const char* slash;
    int         rc;

    if (containerv_disk_path_is_directory(entry)) {
        return _strdup(entry);
    }

    if (PathIsRelativeA(entry)) {
        rc = snprintf(resolved, sizeof(resolved), "%s\\%s", layer_dir, entry);
        if (rc > 0 && (size_t)rc < sizeof(resolved) && containerv_disk_path_is_directory(resolved)) {
            return _strdup(resolved);
        }
    }

    base = strrchr(entry, '\\');
    slash = strrchr(entry, '/');
    if (slash != NULL && (base == NULL || slash > base)) {
        base = slash;
    }
    base = (base != NULL) ? base + 1 : entry;

    rc = snprintf(resolved, sizeof(resolved), "%s\\parents\\%s", layer_dir, base);
    if (rc > 0 && (size_t)rc < sizeof(resolved) && containerv_disk_path_is_directory(resolved)) {
        return _strdup(resolved);
    }
    return NULL;
}

int __windows_layerchain_exists(const char* layer_dir)
{
    char chainPath[MAX_PATH];

    if (__layerchain_path(layer_dir, chainPath, sizeof(chainPath)) != 0) {
        return 0;
    }
    return PathFileExistsA(chainPath) ? 1 : 0;
}

int __windows_layerchain_read(const char* layer_dir, char*** parents_out, int* count_out)
{
    char         chainPath[MAX_PATH];
    json_error_t jerr;
    json_t*      root;
    char**       out;
    int          outCount = 0;
    size_t       n;

    if (parents_out == NULL || count_out == NULL) {
        errno = EINVAL;
        return -1;
    }
    *parents_out = NULL;
    *count_out = 0;

    if (__layerchain_path(layer_dir, chainPath, sizeof(chainPath)) != 0) {
        errno = EINVAL;
        return -1;
    }
    if (!PathFileExistsA(chainPath)) {
        return 0;
    }

    VLOG_DEBUG("containerv[layerchain]", "reading %s\n", chainPath);

    memset(&jerr, 0, sizeof(jerr));
    root = json_load_file(chainPath, 0, &jerr);
    if (root == NULL) {
        VLOG_ERROR("containerv[layerchain]", "failed to parse %s: %s (line %d)\n", chainPath, jerr.text, jerr.line);
        return -1;
    }
    if (!json_is_array(root)) {
        VLOG_ERROR("containerv[layerchain]", "%s is not an array\n", chainPath);
        json_decref(root);
        return -1;
    }

    n = json_array_size(root);
    if (n == 0) {
        // Base layers have an empty chain.
        json_decref(root);
        return 0;
    }

    out = calloc(n, sizeof(char*));
    if (out == NULL) {
        json_decref(root);
        errno = ENOMEM;
        return -1;
    }

    for (size_t i = 0; i < n; i++) {
        const char* entry = json_string_value(json_array_get(root, i));
        if (entry == NULL || entry[0] == '\0') {
            continue;
        }

        out[outCount] = __layerchain_resolve_entry(layer_dir, entry);
        if (out[outCount] == NULL) {
            VLOG_ERROR("containerv[layerchain]", "entry %s in %s could not be resolved to a directory\n", entry, chainPath);
            __windows_strv_free(out, outCount);
            json_decref(root);
            errno = ENOENT;
            return -1;
        }
        outCount++;
    }
    json_decref(root);

    if (outCount == 0) {
        free(out);
        return 0;
    }

    *parents_out = out;
    *count_out = outCount;
    return 0;
}

static int __strv_contains(char* const* values, int count, const char* value)
{
    for (int i = 0; i < count; i++) {
        if (strcmp(values[i], value) == 0) {
            return 1;
        }
    }
    return 0;
}

static int __strv_append_unique(char*** values, int* count, int* capacity, const char* value)
{
    if (__strv_contains(*values, *count, value)) {
        VLOG_ERROR("containerv[layerchain]", "duplicate parent layer in chain: %s\n", value);
        errno = EINVAL;
        return -1;
    }
    if (!containerv_disk_path_is_directory(value)) {
        VLOG_ERROR("containerv[layerchain]", "parent layer path is not a directory: %s\n", value);
        errno = ENOENT;
        return -1;
    }

    if (*count >= *capacity) {
        int    newCapacity = (*capacity == 0) ? 8 : (*capacity * 2);
        char** grown = realloc(*values, (size_t)newCapacity * sizeof(char*));
        if (grown == NULL) {
            errno = ENOMEM;
            return -1;
        }
        *values = grown;
        *capacity = newCapacity;
    }

    (*values)[*count] = _strdup(value);
    if ((*values)[*count] == NULL) {
        errno = ENOMEM;
        return -1;
    }
    (*count)++;
    return 0;
}

int __windows_layerchain_expand(
    const char* const* parents,
    int                count,
    char***            expanded_out,
    int*               expanded_count_out)
{
    char** out = NULL;
    int    outCount = 0;
    int    outCapacity = 0;

    if (expanded_out == NULL || expanded_count_out == NULL || parents == NULL || count <= 0) {
        errno = EINVAL;
        return -1;
    }
    *expanded_out = NULL;
    *expanded_count_out = 0;

    for (int i = 0; i < count; i++) {
        char** extra = NULL;
        int    extraCount = 0;

        if (parents[i] == NULL || parents[i][0] == '\0') {
            continue;
        }
        if (__strv_append_unique(&out, &outCount, &outCapacity, parents[i]) != 0) {
            goto error;
        }

        // Chains sometimes only list immediate parents; pull in each parent's own chain.
        if (__windows_layerchain_read(parents[i], &extra, &extraCount) != 0) {
            VLOG_ERROR("containerv[layerchain]", "failed to read parent chain under %s\n", parents[i]);
            goto error;
        }
        for (int j = 0; j < extraCount; j++) {
            if (__strv_append_unique(&out, &outCount, &outCapacity, extra[j]) != 0) {
                __windows_strv_free(extra, extraCount);
                goto error;
            }
        }
        __windows_strv_free(extra, extraCount);
    }

    if (outCount == 0) {
        errno = EINVAL;
        goto error;
    }

    *expanded_out = out;
    *expanded_count_out = outCount;
    return 0;

error:
    __windows_strv_free(out, outCount);
    return -1;
}

int __windows_layerchain_write(const char* layer_dir, const char* const* parents, int count)
{
    char    chainPath[MAX_PATH];
    json_t* root;
    int     status;

    if (__layerchain_path(layer_dir, chainPath, sizeof(chainPath)) != 0) {
        errno = EINVAL;
        return -1;
    }

    root = json_array();
    if (root == NULL) {
        errno = ENOMEM;
        return -1;
    }

    for (int i = 0; i < count; i++) {
        if (parents[i] == NULL || parents[i][0] == '\0') {
            continue;
        }
        if (json_array_append_new(root, json_string(parents[i])) != 0) {
            json_decref(root);
            errno = ENOMEM;
            return -1;
        }
    }

    status = json_dump_file(root, chainPath, JSON_COMPACT);
    json_decref(root);
    if (status != 0) {
        VLOG_ERROR("containerv[layerchain]", "failed to write %s\n", chainPath);
        return -1;
    }
    return 0;
}
