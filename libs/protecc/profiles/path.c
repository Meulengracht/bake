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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <protecc/profile.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>

#include "../private.h"

size_t __profile_size(uint32_t nodeCount, uint32_t edgeCount)
{
    return sizeof(protecc_profile_header_t)
        + (size_t)nodeCount * sizeof(protecc_profile_node_t)
        + (size_t)edgeCount * sizeof(uint32_t);
}

protecc_error_t __update_stats_trie_profile(protecc_profile_t* profile)
{
    size_t nodeCount = 0;
    size_t maxDepth = 0;
    size_t edgeCount = 0;

    if (profile->root == NULL) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }
    
    protecc_node_collect_stats(profile->root, 0, &nodeCount, &maxDepth, &edgeCount);
    if (nodeCount > UINT32_MAX || edgeCount > UINT32_MAX) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    profile->stats.num_nodes = nodeCount;
    profile->stats.max_depth = maxDepth;
    profile->stats.binary_size = __profile_size((uint32_t)nodeCount, (uint32_t)edgeCount);

    return PROTECC_OK;
}

static protecc_error_t __read_and_validate_profile_header(
    const void*               buffer,
    size_t                    bufferSize,
    protecc_profile_header_t* header)
{
    if (buffer == NULL || header == NULL || bufferSize < sizeof(protecc_profile_header_t)) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    memcpy(header, buffer, sizeof(*header));

    if (header->magic != PROTECC_PROFILE_MAGIC || header->version != PROTECC_PROFILE_VERSION) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    if ((header->flags & (PROTECC_PROFILE_FLAG_TYPE_TRIE | PROTECC_PROFILE_FLAG_TYPE_DFA)) == 0) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    return PROTECC_OK;
}

protecc_error_t protecc_profile_export_path(
    const protecc_profile_t* profile,
    void*                    buffer,
    size_t                   buffer_size,
    size_t*                  bytes_written
) {
    protecc_error_t err;

    if (profile == NULL) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    err = __update_stats_trie_profile((protecc_profile_t*)profile);
    if (err != PROTECC_OK) {
        return err;
    }

    if (profile->stats.num_patterns > UINT32_MAX) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    if (profile->config.mode == PROTECC_COMPILE_MODE_DFA) {
        return __export_dfa_profile(profile, buffer, buffer_size, bytes_written);
    }
    return __export_trie_profile(profile, buffer, buffer_size, bytes_written);
}

protecc_error_t protecc_profile_import_path_blob(
    const void*         buffer,
    size_t              bufferSize,
    protecc_profile_t** compiled
) {
    const uint8_t*           base = (const uint8_t*)buffer;
    protecc_profile_header_t header;
    protecc_error_t          err;

    if (compiled == NULL || bufferSize < sizeof(uint32_t) * 3) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    err = __read_and_validate_profile_header(buffer, bufferSize, &header);
    if (err != PROTECC_OK) {
        return err;
    }

    if ((header.flags & PROTECC_PROFILE_FLAG_TYPE_DFA) != 0) {
        return __import_dfa_profile(base, bufferSize, &header, compiled);
    }
    return __import_trie_profile(base, bufferSize, &header, compiled);
}

protecc_error_t protecc_validate_pattern(const char* pattern) {
    int depth = 0;

    if (pattern == NULL) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }
    
    // Basic validation - check for balanced brackets
    for (const char* p = pattern; *p; p++) {
        if (*p == '[') {
            depth++;
        } else if (*p == ']') {
            depth--;
            if (depth < 0) {
                return PROTECC_ERROR_INVALID_PATTERN;
            }
        }
    }
    
    if (depth != 0) {
        return PROTECC_ERROR_INVALID_PATTERN;
    }
    
    return PROTECC_OK;
}
