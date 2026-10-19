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

    if (!PathIsRelativeA(entry) && containerv_disk_path_is_directory(entry)) {
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

struct layer_node {
    char* path;
    BY_HANDLE_FILE_INFORMATION identity;
    int* after;
    int after_count;
    int incoming;
    int emitted;
};

struct layer_graph {
    struct layer_node* nodes;
    int count;
};

static int __layer_node(struct layer_graph* graph, const char* path)
{
    BY_HANDLE_FILE_INFORMATION identity;
    struct layer_node* grown;
    wchar_t* wide = __windows_utf8_to_wide_alloc(path);
    HANDLE directory;
    if (!wide) return -1;
    directory = CreateFileW(wide, FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS, NULL);
    free(wide);
    if (directory == INVALID_HANDLE_VALUE) { errno = ENOENT; return -1; }
    int ok = GetFileInformationByHandle(directory, &identity);
    CloseHandle(directory);
    if (!ok || !(identity.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
        errno = ENOTDIR;
        return -1;
    }
    // Directory identity also deduplicates relative paths and junction aliases,
    // without conflating distinct names on case-sensitive volumes.
    for (int i = 0; i < graph->count; ++i) {
        BY_HANDLE_FILE_INFORMATION* other = &graph->nodes[i].identity;
        if (identity.dwVolumeSerialNumber == other->dwVolumeSerialNumber &&
            identity.nFileIndexHigh == other->nFileIndexHigh &&
            identity.nFileIndexLow == other->nFileIndexLow) return i;
    }
    char* copy = _strdup(path);
    if (!copy) return -1;
    grown = realloc(graph->nodes, (graph->count + 1) * sizeof(*grown));
    if (!grown) { free(copy); return -1; }
    graph->nodes = grown;
    memset(&grown[graph->count], 0, sizeof(*grown));
    grown[graph->count].path = copy;
    grown[graph->count].identity = identity;
    return graph->count++;
}

static int __layer_before(struct layer_graph* graph, int before, int after)
{
    struct layer_node* node = &graph->nodes[before];
    for (int i = 0; i < node->after_count; ++i) {
        if (node->after[i] == after) return 0;
    }
    int* grown = realloc(node->after, (node->after_count + 1) * sizeof(int));
    if (!grown) return -1;
    node->after = grown;
    node->after[node->after_count++] = after;
    graph->nodes[after].incoming++;
    return 0;
}

int __windows_layerchain_expand(
    const char* const* parents, int count, char*** expanded_out, int* expanded_count_out)
{
    struct layer_graph graph = {0};
    char** out = NULL;
    int previous = -1, status = -1, out_count = 0;
    if (!parents || count <= 0 || !expanded_out || !expanded_count_out) {
        errno = EINVAL;
        return -1;
    }
    *expanded_out = NULL;
    *expanded_count_out = 0;
    // Preserve the caller's top-to-bottom ordering as well as each parent's
    // ancestry. Repeated entries are overlaps, not cycles by themselves.
    for (int i = 0; i < count; ++i) {
        if (!parents[i] || !parents[i][0]) continue;
        int before_count = graph.count;
        int node = __layer_node(&graph, parents[i]);
        if (node < 0) goto cleanup;
        if (node < before_count) continue;
        if (previous >= 0 && __layer_before(&graph, previous, node) != 0) goto cleanup;
        previous = node;
    }
    for (int i = 0; i < graph.count; ++i) {
        char** ancestry = NULL;
        int ancestry_count = 0;
        if (__windows_layerchain_read(graph.nodes[i].path, &ancestry, &ancestry_count) != 0) goto cleanup;
        int* seen = ancestry_count ? calloc(ancestry_count, sizeof(int)) : NULL;
        int seen_count = 0;
        if (ancestry_count && !seen) { __windows_strv_free(ancestry, ancestry_count); goto cleanup; }
        previous = i;
        for (int j = 0; j < ancestry_count; ++j) {
            int node = __layer_node(&graph, ancestry[j]);
            int duplicate = 0;
            for (int k = 0; k < seen_count; ++k) if (seen[k] == node) duplicate = 1;
            if (duplicate) continue;
            if (node < 0 || __layer_before(&graph, previous, node) != 0) {
                free(seen);
                __windows_strv_free(ancestry, ancestry_count);
                goto cleanup;
            }
            seen[seen_count++] = node;
            previous = node;
        }
        free(seen);
        __windows_strv_free(ancestry, ancestry_count);
    }
    if (!graph.count) { errno = EINVAL; goto cleanup; }
    out = calloc(graph.count, sizeof(char*));
    if (!out) goto cleanup;
    // Stable topological ordering distinguishes shared ancestry from cycles,
    // including cycles reached through chains of immediate parents.
    while (out_count < graph.count) {
        int next = -1;
        for (int i = 0; i < graph.count; ++i) {
            if (!graph.nodes[i].emitted && graph.nodes[i].incoming == 0) { next = i; break; }
        }
        if (next < 0) {
            VLOG_ERROR("containerv[layerchain]", "cyclic or inconsistent parent layer order\n");
            errno = EINVAL;
            goto cleanup;
        }
        struct layer_node* node = &graph.nodes[next];
        out[out_count++] = node->path;
        node->path = NULL;
        node->emitted = 1;
        for (int j = 0; j < node->after_count; ++j) graph.nodes[node->after[j]].incoming--;
    }
    *expanded_out = out;
    *expanded_count_out = out_count;
    out = NULL;
    status = 0;
cleanup:
    __windows_strv_free(out, out_count);
    for (int i = 0; i < graph.count; ++i) {
        free(graph.nodes[i].path);
        free(graph.nodes[i].after);
    }
    free(graph.nodes);
    return status;
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
