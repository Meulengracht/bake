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

#include "../../private.h"

protecc_node_t* protecc_node_new(protecc_node_type_t type)
{
    protecc_node_t* node = calloc(1, sizeof(protecc_node_t));
    if (node == NULL) {
        return NULL;
    }
    
    node->type = type;
    node->modifier = MODIFIER_NONE;
    node->children = NULL;
    node->num_children = 0;
    node->capacity_children = 0;
    node->is_terminal = false;
    
    return node;
}

void protecc_node_free(protecc_node_t* node) {
    if (node == NULL) {
        return;
    }
    
    // Free all children recursively
    for (size_t i = 0; i < node->num_children; i++) {
        protecc_node_free(node->children[i]);
    }
    
    free(node->children);
    free(node);
}

protecc_error_t protecc_node_add_child(protecc_node_t* parent, protecc_node_t* child) {
    if (parent == NULL || child == NULL) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }
    
    // Check if we need to expand capacity
    if (parent->num_children >= parent->capacity_children) {
        size_t           newCapacity = parent->capacity_children == 0 ? 4 : parent->capacity_children * 2;
        protecc_node_t** newChildren = realloc(
            parent->children,
            newCapacity * sizeof(protecc_node_t*)
        );
        if (newChildren == NULL) {
            return PROTECC_ERROR_OUT_OF_MEMORY;
        }
        
        parent->children = newChildren;
        parent->capacity_children = newCapacity;
    }
    
    parent->children[parent->num_children++] = child;
    return PROTECC_OK;
}

void protecc_charset_set(protecc_charset_t* charset, unsigned char c) {
    if (charset == NULL) {
        return;
    }
    charset->chars[c / 8] |= (1 << (c % 8));
}

bool protecc_charset_contains(const protecc_charset_t* charset, unsigned char c) {
    if (charset == NULL) {
        return false;
    }
    return (charset->chars[c / 8] & (1 << (c % 8))) != 0;
}

void protecc_charset_set_range(protecc_charset_t* charset, char start, char end) {
    // Handle unsigned char properly to avoid overflow at 255
    unsigned char ustart = (unsigned char)start;
    unsigned char uend = (unsigned char)end;
    
    if (charset == NULL) {
        return;
    }

    for (unsigned int c = ustart; c <= uend; c++) {
        protecc_charset_set(charset, (unsigned char)c);

        // Prevent overflow
        if (c == 255) {
            break;
        }
    }
}

void protecc_node_collect_stats(
    const protecc_node_t* node,
    size_t                depth,
    size_t*               numNodes,
    size_t*               maxDepth,
    size_t*               numEdges)
{
    if (node == NULL) {
        return;
    }

    (*numNodes)++;
    if (depth > *maxDepth) {
        *maxDepth = depth;
    }
    *numEdges += node->num_children;

    for (size_t i = 0; i < node->num_children; i++) {
        protecc_node_collect_stats(node->children[i], depth + 1, numNodes, maxDepth, numEdges);
    }
}
