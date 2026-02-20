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

protecc_error_t __update_stats_trie_profile(protecc_profile_t* compiled)
{
    size_t num_nodes = 0;
    size_t max_depth = 0;
    size_t num_edges = 0;

    if (!compiled || !compiled->root) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }
    protecc_node_collect_stats(compiled->root, 0, &num_nodes, &max_depth, &num_edges);

    if (num_nodes > UINT32_MAX || num_edges > UINT32_MAX) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    compiled->stats.num_nodes = num_nodes;
    compiled->stats.max_depth = max_depth;
    compiled->stats.binary_size = __profile_size((uint32_t)num_nodes, (uint32_t)num_edges);

    return PROTECC_OK;
}

static protecc_error_t __read_and_validate_profile_header(
    const void* buffer,
    size_t buffer_size,
    protecc_profile_header_t* header)
{
    if (!buffer || !header || buffer_size < sizeof(protecc_profile_header_t)) {
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
    const protecc_profile_t* compiled,
    void*                    buffer,
    size_t                   buffer_size,
    size_t*                  bytes_written
) {
    if (!compiled) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    protecc_error_t stats_err = __update_stats_trie_profile((protecc_profile_t*)compiled);
    if (stats_err != PROTECC_OK) {
        return stats_err;
    }

    if (compiled->stats.num_patterns > UINT32_MAX) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    if (compiled->config.mode == PROTECC_COMPILE_MODE_DFA) {
        return __export_dfa_profile(compiled, buffer, buffer_size, bytes_written);
    }
    return __export_trie_profile(compiled, buffer, buffer_size, bytes_written);
}

protecc_error_t protecc_profile_import_path_blob(
    const void* buffer,
    size_t buffer_size,
    protecc_profile_t** compiled
) {
    const uint8_t*           base = (const uint8_t*)buffer;
    protecc_profile_header_t header;
    protecc_error_t          header_err;

    if (!compiled || buffer_size < sizeof(uint32_t) * 3) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    header_err = __read_and_validate_profile_header(buffer, buffer_size, &header);
    if (header_err != PROTECC_OK) {
        return header_err;
    }

    if ((header.flags & PROTECC_PROFILE_FLAG_TYPE_DFA) != 0) {
        return __import_dfa_profile(base, buffer_size, &header, compiled);
    }
    return __import_trie_profile(base, buffer_size, &header, compiled);
}

protecc_error_t protecc_validate_pattern(const char* pattern) {
    if (!pattern) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }
    
    // Basic validation - check for balanced brackets
    int bracket_depth = 0;
    for (const char* p = pattern; *p; p++) {
        if (*p == '[') {
            bracket_depth++;
        } else if (*p == ']') {
            bracket_depth--;
            if (bracket_depth < 0) {
                return PROTECC_ERROR_INVALID_PATTERN;
            }
        }
    }
    
    if (bracket_depth != 0) {
        return PROTECC_ERROR_INVALID_PATTERN;
    }
    
    return PROTECC_OK;
}
