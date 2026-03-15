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

#include "../../private.h"

static size_t __find_node_index(
    const protecc_node_t* const* nodes,
    size_t                       count,
    const protecc_node_t*        target)
{
    for (size_t i = 0; i < count; i++) {
        if (nodes[i] == target) {
            return i;
        }
    }
    return SIZE_MAX;
}

static void __collect_nodes(
    const protecc_node_t*  node,
    const protecc_node_t** nodes,
    size_t*                index)
{
    if (node == NULL) {
        return;
    }

    nodes[(*index)++] = node;
    for (size_t i = 0; i < node->num_children; i++) {
        __collect_nodes(node->children[i], nodes, index);
    }
}

protecc_error_t __export_trie_profile(
    const protecc_profile_t* compiled,
    void*                    buffer,
    size_t                   bufferSize,
    size_t*                  bytesWritten)
{
    const protecc_node_t**   nodes;
    protecc_profile_header_t header;
    protecc_profile_node_t*  profileNodes;
    uint8_t*                 out;
    uint32_t*                edges;
    size_t                   edgeIndex;
    size_t                   edgeCount = 0;
    size_t                   nodeCount = 0;
    size_t                   maxDepth = 0;
    size_t                   requiredSize;
    size_t                   index;

    if (compiled->stats.num_nodes > UINT32_MAX) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    protecc_node_collect_stats(compiled->root, 0, &nodeCount, &maxDepth, &edgeCount);
    if (edgeCount > UINT32_MAX) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    requiredSize = __profile_size((uint32_t)nodeCount, (uint32_t)edgeCount);
    if (requiredSize > UINT32_MAX) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    if (bytesWritten) {
        *bytesWritten = requiredSize;
    }

    if (buffer == NULL) {
        return PROTECC_OK;
    }

    if (bufferSize < requiredSize) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    nodes = calloc(nodeCount, sizeof(*nodes));
    if (nodes == NULL) {
        return PROTECC_ERROR_OUT_OF_MEMORY;
    }

    index = 0;
    __collect_nodes(compiled->root, nodes, &index);
    if (index != nodeCount) {
        free(nodes);
        return PROTECC_ERROR_COMPILE_FAILED;
    }

    out = (uint8_t*)buffer;
    header.magic = PROTECC_PROFILE_MAGIC;
    header.version = PROTECC_PROFILE_VERSION;
    header.flags = (compiled->flags & ~(PROTECC_PROFILE_FLAG_TYPE_TRIE | PROTECC_PROFILE_FLAG_TYPE_DFA))
                 | PROTECC_PROFILE_FLAG_TYPE_TRIE;
    header.num_nodes = (uint32_t)nodeCount;
    header.num_edges = (uint32_t)edgeCount;
    header.root_index = 0;
    header.stats.num_patterns = (uint32_t)compiled->stats.num_patterns;
    header.stats.binary_size = (uint32_t)requiredSize;
    header.stats.max_depth = (uint32_t)compiled->stats.max_depth;
    header.stats.num_nodes = (uint32_t)compiled->stats.num_nodes;
    memcpy(out, &header, sizeof(header));

    profileNodes = (protecc_profile_node_t*)(out + sizeof(header));
    edges = (uint32_t*)(out + sizeof(header) + nodeCount * sizeof(protecc_profile_node_t));

    edgeIndex = 0;
    for (size_t i = 0; i < nodeCount; i++) {
        const protecc_node_t* node = nodes[i];
        protecc_profile_node_t profile = {0};
        
        profile.type = (uint8_t)node->type;
        profile.modifier = (uint8_t)node->modifier;
        profile.is_terminal = node->is_terminal ? 1 : 0;
        profile.child_start = (uint32_t)edgeIndex;
        profile.child_count = (uint16_t)node->num_children;
        profile.perms = (uint32_t)node->perms;

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

        profileNodes[i] = profile;

        for (size_t c = 0; c < node->num_children; c++) {
            size_t childIndex = __find_node_index(nodes, nodeCount, node->children[c]);
            if (childIndex == SIZE_MAX || childIndex > UINT32_MAX) {
                free(nodes);
                return PROTECC_ERROR_COMPILE_FAILED;
            }
            edges[edgeIndex++] = (uint32_t)childIndex;
        }
    }

    free(nodes);
    return PROTECC_OK;
}

static void __free_import_nodes(protecc_node_t** nodes, uint32_t count)
{
    if (nodes == NULL) {
        return;
    }

    for (uint32_t i = 0; i < count; i++) {
        if (nodes[i] != NULL) {
            free(nodes[i]->children);
            free(nodes[i]);
        }
    }
    free(nodes);
}

static protecc_error_t __cleanup_import_trie_failure(
    protecc_node_t**   nodes,
    uint32_t           nodeCount,
    protecc_profile_t* profile,
    protecc_error_t    error)
{
    __free_import_nodes(nodes, nodeCount);
    free(profile);
    return error;
}

protecc_error_t __import_trie_profile(
    const uint8_t*                  base,
    size_t                          bufferSize,
    const protecc_profile_header_t* header,
    protecc_profile_t**             profileOut)
{
    size_t                        requiredSize = __profile_size(header->num_nodes, header->num_edges);
    const protecc_profile_node_t* profileNodes;
    const uint32_t*               edges;
    protecc_profile_t*            profile;
    protecc_node_t**              nodes;

    if (bufferSize < requiredSize) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    profileNodes = (const protecc_profile_node_t*)(base + sizeof(protecc_profile_header_t));
    edges = (const uint32_t*)(base + sizeof(protecc_profile_header_t)
                              + (size_t)header->num_nodes * sizeof(protecc_profile_node_t));

    profile = calloc(1, sizeof(protecc_profile_t));
    if (profile == NULL) {
        return PROTECC_ERROR_OUT_OF_MEMORY;
    }

    if (header->root_index >= header->num_nodes) {
        free(profile);
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    nodes = calloc(header->num_nodes, sizeof(*nodes));
    if (nodes == NULL) {
        free(profile);
        return PROTECC_ERROR_OUT_OF_MEMORY;
    }

    for (uint32_t i = 0; i < header->num_nodes; i++) {
        protecc_node_t* node = protecc_node_new((protecc_node_type_t)profileNodes[i].type);
        if (node == NULL) {
            return __cleanup_import_trie_failure(nodes, header->num_nodes, profile,
                                                 PROTECC_ERROR_OUT_OF_MEMORY);
        }

        node->modifier = (protecc_modifier_t)profileNodes[i].modifier;
        node->is_terminal = profileNodes[i].is_terminal != 0;
        node->perms = (protecc_permission_t)profileNodes[i].perms;

        switch (node->type) {
            case NODE_LITERAL:
                node->data.literal = (char)profileNodes[i].data.literal;
                break;
            case NODE_RANGE:
                node->data.range.start = (char)profileNodes[i].data.range.start;
                node->data.range.end = (char)profileNodes[i].data.range.end;
                break;
            case NODE_CHARSET:
                memcpy(node->data.charset.chars, profileNodes[i].data.charset,
                       sizeof(node->data.charset.chars));
                break;
            default:
                break;
        }

        nodes[i] = node;
    }

    for (uint32_t i = 0; i < header->num_nodes; i++) {
        protecc_node_t* node = nodes[i];
        uint16_t        childCount = profileNodes[i].child_count;
        uint32_t        childStart = profileNodes[i].child_start;

        if (childCount == 0) {
            continue;
        }

        if ((uint32_t)childStart + childCount > header->num_edges) {
            return __cleanup_import_trie_failure(nodes, header->num_nodes, profile,
                                                 PROTECC_ERROR_INVALID_ARGUMENT);
        }

        node->children = calloc(childCount, sizeof(*node->children));
        if (node->children == NULL) {
            return __cleanup_import_trie_failure(nodes, header->num_nodes, profile,
                                                 PROTECC_ERROR_OUT_OF_MEMORY);
        }

        node->capacity_children = childCount;
        node->num_children = childCount;
        for (uint16_t c = 0; c < childCount; c++) {
            uint32_t childIndex = edges[childStart + c];
            if (childIndex >= header->num_nodes) {
                return __cleanup_import_trie_failure(nodes, header->num_nodes, profile,
                                                     PROTECC_ERROR_INVALID_ARGUMENT);
            }
            node->children[c] = nodes[childIndex];
        }
    }

    profile->root = nodes[header->root_index];
    profile->flags = header->flags & ~(PROTECC_PROFILE_FLAG_TYPE_TRIE | PROTECC_PROFILE_FLAG_TYPE_DFA);
    profile->stats.num_patterns = header->stats.num_patterns;
    profile->stats.binary_size = header->stats.binary_size;
    profile->stats.max_depth = header->stats.max_depth;
    profile->stats.num_nodes = header->stats.num_nodes;

    protecc_compile_config_default(&profile->config);

    free(nodes);
    *profileOut = profile;
    return PROTECC_OK;
}
