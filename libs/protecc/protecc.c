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

#include "private.h"

static void protecc_collect_stats(
    const protecc_node_t* node,
    size_t depth,
    size_t* num_nodes,
    size_t* max_depth,
    size_t* num_edges
) {
    if (!node) {
        return;
    }

    (*num_nodes)++;
    if (depth > *max_depth) {
        *max_depth = depth;
    }
    *num_edges += node->num_children;

    for (size_t i = 0; i < node->num_children; i++) {
        protecc_collect_stats(node->children[i], depth + 1, num_nodes, max_depth, num_edges);
    }
}

static void protecc_collect_nodes(
    const protecc_node_t* node,
    const protecc_node_t** nodes,
    size_t* index
) {
    if (!node) {
        return;
    }

    nodes[(*index)++] = node;
    for (size_t i = 0; i < node->num_children; i++) {
        protecc_collect_nodes(node->children[i], nodes, index);
    }
}

static size_t protecc_find_node_index(
    const protecc_node_t* const* nodes,
    size_t count,
    const protecc_node_t* target
) {
    for (size_t i = 0; i < count; i++) {
        if (nodes[i] == target) {
            return i;
        }
    }
    return SIZE_MAX;
}

static size_t protecc_profile_size(uint32_t num_nodes, uint32_t num_edges) {
    return sizeof(protecc_profile_header_t)
        + (size_t)num_nodes * sizeof(protecc_profile_node_t)
        + (size_t)num_edges * sizeof(uint32_t);
}

static protecc_error_t protecc_update_stats(protecc_compiled_t* compiled) {
    if (!compiled || !compiled->root) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    size_t num_nodes = 0;
    size_t max_depth = 0;
    size_t num_edges = 0;

    protecc_collect_stats(compiled->root, 0, &num_nodes, &max_depth, &num_edges);

    if (num_nodes > UINT32_MAX || num_edges > UINT32_MAX) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    compiled->stats.num_nodes = num_nodes;
    compiled->stats.max_depth = max_depth;
    compiled->stats.binary_size = protecc_profile_size((uint32_t)num_nodes, (uint32_t)num_edges);

    return PROTECC_OK;
}

const char* protecc_error_string(protecc_error_t error) {
    switch (error) {
        case PROTECC_OK:
            return "Success";
        case PROTECC_ERROR_INVALID_PATTERN:
            return "Invalid pattern";
        case PROTECC_ERROR_OUT_OF_MEMORY:
            return "Out of memory";
        case PROTECC_ERROR_INVALID_ARGUMENT:
            return "Invalid argument";
        case PROTECC_ERROR_COMPILE_FAILED:
            return "Compilation failed";
        default:
            return "Unknown error";
    }
}

protecc_error_t protecc_compile(
    const char** patterns,
    size_t count,
    uint32_t flags,
    protecc_compiled_t** compiled
) {
    if (!patterns || !compiled || count == 0) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }
    
    protecc_compiled_t* comp = calloc(1, sizeof(protecc_compiled_t));
    if (!comp) {
        return PROTECC_ERROR_OUT_OF_MEMORY;
    }
    
    comp->root = protecc_node_new(NODE_LITERAL);
    if (!comp->root) {
        free(comp);
        return PROTECC_ERROR_OUT_OF_MEMORY;
    }
    
    comp->flags = flags;
    comp->stats.num_patterns = count;
    
    // Parse each pattern and add to trie
    for (size_t i = 0; i < count; i++) {
        protecc_error_t err = protecc_parse_pattern(patterns[i], comp->root, flags);
        if (err != PROTECC_OK) {
            protecc_free(comp);
            return err;
        }
    }
    
    // Calculate statistics
    protecc_error_t stats_err = protecc_update_stats(comp);
    if (stats_err != PROTECC_OK) {
        protecc_free(comp);
        return stats_err;
    }
    
    *compiled = comp;
    return PROTECC_OK;
}

bool protecc_match(
    const protecc_compiled_t* compiled,
    const char* path,
    size_t path_len
) {
    if (!compiled || !compiled->root || !path) {
        return false;
    }
    
    if (path_len == 0) {
        path_len = strlen(path);
    }
    
    return protecc_match_internal(compiled->root, path, path_len, 0, compiled->flags);
}

protecc_error_t protecc_get_stats(
    const protecc_compiled_t* compiled,
    protecc_stats_t* stats
) {
    if (!compiled || !stats) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }
    
    *stats = compiled->stats;
    return PROTECC_OK;
}

protecc_error_t protecc_export(
    const protecc_compiled_t* compiled,
    void* buffer,
    size_t buffer_size,
    size_t* bytes_written
) {
    if (!compiled) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    protecc_error_t stats_err = protecc_update_stats((protecc_compiled_t*)compiled);
    if (stats_err != PROTECC_OK) {
        return stats_err;
    }

    if (compiled->stats.num_patterns > UINT32_MAX) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    if (compiled->stats.num_nodes > UINT32_MAX) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    size_t num_edges = 0;
    size_t num_nodes = 0;
    size_t max_depth = 0;
    protecc_collect_stats(compiled->root, 0, &num_nodes, &max_depth, &num_edges);
    if (num_edges > UINT32_MAX) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    size_t required_size = protecc_profile_size((uint32_t)num_nodes, (uint32_t)num_edges);
    if (required_size > UINT32_MAX) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }
    
    if (bytes_written) {
        *bytes_written = required_size;
    }
    
    if (!buffer) {
        // Just querying size
        return PROTECC_OK;
    }
    
    if (buffer_size < required_size) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }
    
    const protecc_node_t** nodes = calloc(num_nodes, sizeof(*nodes));
    if (!nodes) {
        return PROTECC_ERROR_OUT_OF_MEMORY;
    }

    size_t index = 0;
    protecc_collect_nodes(compiled->root, nodes, &index);
    if (index != num_nodes) {
        free(nodes);
        return PROTECC_ERROR_COMPILE_FAILED;
    }

    uint8_t* out = (uint8_t*)buffer;
    protecc_profile_header_t header;
    header.magic = PROTECC_PROFILE_MAGIC;
    header.version = PROTECC_PROFILE_VERSION;
    header.flags = compiled->flags;
    header.num_nodes = (uint32_t)num_nodes;
    header.num_edges = (uint32_t)num_edges;
    header.root_index = 0;
    header.stats.num_patterns = (uint32_t)compiled->stats.num_patterns;
    header.stats.binary_size = (uint32_t)required_size;
    header.stats.max_depth = (uint32_t)compiled->stats.max_depth;
    header.stats.num_nodes = (uint32_t)compiled->stats.num_nodes;
    memcpy(out, &header, sizeof(header));

    protecc_profile_node_t* profile_nodes = (protecc_profile_node_t*)(out + sizeof(header));
    uint32_t* edges = (uint32_t*)(out + sizeof(header) + num_nodes * sizeof(protecc_profile_node_t));

    size_t edge_offset = 0;
    for (size_t i = 0; i < num_nodes; i++) {
        const protecc_node_t* node = nodes[i];
        protecc_profile_node_t profile = {0};
        profile.type = (uint8_t)node->type;
        profile.modifier = (uint8_t)node->modifier;
        profile.is_terminal = node->is_terminal ? 1 : 0;
        profile.child_start = (uint32_t)edge_offset;
        profile.child_count = (uint16_t)node->num_children;

        if (node->num_children > UINT16_MAX) {
            free(nodes);
            return PROTECC_ERROR_INVALID_ARGUMENT;
        }

        switch (node->type) {
            case NODE_LITERAL:
                profile.data.literal = (uint8_t)node->data.literal;
                break;
            case NODE_RANGE:
                profile.data.range.start = (uint8_t)node->data.range.start;
                profile.data.range.end = (uint8_t)node->data.range.end;
                break;
            case NODE_CHARSET:
                memcpy(profile.data.charset, node->data.charset.chars, sizeof(profile.data.charset));
                break;
            default:
                break;
        }

        profile_nodes[i] = profile;

        for (size_t c = 0; c < node->num_children; c++) {
            size_t child_index = protecc_find_node_index(nodes, num_nodes, node->children[c]);
            if (child_index == SIZE_MAX || child_index > UINT32_MAX) {
                free(nodes);
                return PROTECC_ERROR_COMPILE_FAILED;
            }
            edges[edge_offset++] = (uint32_t)child_index;
        }
    }

    free(nodes);
    return PROTECC_OK;
}

protecc_error_t protecc_import(
    const void* buffer,
    size_t buffer_size,
    protecc_compiled_t** compiled
) {
    if (!buffer || !compiled || buffer_size < sizeof(uint32_t) * 3) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    if (buffer_size < sizeof(protecc_profile_header_t)) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    protecc_profile_header_t header;
    memcpy(&header, buffer, sizeof(header));

    if (header.magic != PROTECC_PROFILE_MAGIC || header.version != PROTECC_PROFILE_VERSION) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    size_t required_size = protecc_profile_size(header.num_nodes, header.num_edges);
    if (buffer_size < required_size) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    const uint8_t* base = (const uint8_t*)buffer;
    const protecc_profile_node_t* profile_nodes =
        (const protecc_profile_node_t*)(base + sizeof(protecc_profile_header_t));
    const uint32_t* edges =
        (const uint32_t*)(base + sizeof(protecc_profile_header_t)
                          + (size_t)header.num_nodes * sizeof(protecc_profile_node_t));

    protecc_compiled_t* comp = calloc(1, sizeof(protecc_compiled_t));
    if (!comp) {
        return PROTECC_ERROR_OUT_OF_MEMORY;
    }

    if (header.root_index >= header.num_nodes) {
        free(comp);
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    protecc_node_t** nodes = calloc(header.num_nodes, sizeof(*nodes));
    if (!nodes) {
        free(comp);
        return PROTECC_ERROR_OUT_OF_MEMORY;
    }

    for (uint32_t i = 0; i < header.num_nodes; i++) {
        protecc_node_t* node = protecc_node_new((protecc_node_type_t)profile_nodes[i].type);
        if (!node) {
            for (uint32_t j = 0; j < header.num_nodes; j++) {
                if (nodes[j]) {
                    free(nodes[j]->children);
                    free(nodes[j]);
                }
            }
            free(nodes);
            free(comp);
            return PROTECC_ERROR_OUT_OF_MEMORY;
        }

        node->modifier = (protecc_modifier_t)profile_nodes[i].modifier;
        node->is_terminal = profile_nodes[i].is_terminal != 0;

        switch (node->type) {
            case NODE_LITERAL:
                node->data.literal = (char)profile_nodes[i].data.literal;
                break;
            case NODE_RANGE:
                node->data.range.start = (char)profile_nodes[i].data.range.start;
                node->data.range.end = (char)profile_nodes[i].data.range.end;
                break;
            case NODE_CHARSET:
                memcpy(node->data.charset.chars, profile_nodes[i].data.charset,
                       sizeof(node->data.charset.chars));
                break;
            default:
                break;
        }

        nodes[i] = node;
    }

    for (uint32_t i = 0; i < header.num_nodes; i++) {
        protecc_node_t* node = nodes[i];
        uint16_t child_count = profile_nodes[i].child_count;
        uint32_t child_start = profile_nodes[i].child_start;

        if (child_count == 0) {
            continue;
        }

        if ((uint32_t)child_start + child_count > header.num_edges) {
            for (uint32_t j = 0; j < header.num_nodes; j++) {
                if (nodes[j]) {
                    free(nodes[j]->children);
                    free(nodes[j]);
                }
            }
            free(nodes);
            free(comp);
            return PROTECC_ERROR_INVALID_ARGUMENT;
        }

        node->children = calloc(child_count, sizeof(*node->children));
        if (!node->children) {
            for (uint32_t j = 0; j < header.num_nodes; j++) {
                if (nodes[j]) {
                    free(nodes[j]->children);
                    free(nodes[j]);
                }
            }
            free(nodes);
            free(comp);
            return PROTECC_ERROR_OUT_OF_MEMORY;
        }

        node->capacity_children = child_count;
        node->num_children = child_count;
        for (uint16_t c = 0; c < child_count; c++) {
            uint32_t child_index = edges[child_start + c];
            if (child_index >= header.num_nodes) {
                for (uint32_t j = 0; j < header.num_nodes; j++) {
                    if (nodes[j]) {
                        free(nodes[j]->children);
                        free(nodes[j]);
                    }
                }
                free(nodes);
                free(comp);
                return PROTECC_ERROR_INVALID_ARGUMENT;
            }
            node->children[c] = nodes[child_index];
        }
    }

    comp->root = nodes[header.root_index];
    comp->flags = header.flags;
    comp->stats.num_patterns = header.stats.num_patterns;
    comp->stats.binary_size = header.stats.binary_size;
    comp->stats.max_depth = header.stats.max_depth;
    comp->stats.num_nodes = header.stats.num_nodes;

    free(nodes);
    *compiled = comp;
    return PROTECC_OK;
}

void protecc_free(protecc_compiled_t* compiled) {
    if (!compiled) {
        return;
    }
    
    if (compiled->root) {
        protecc_node_free(compiled->root);
    }
    
    free(compiled);
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
