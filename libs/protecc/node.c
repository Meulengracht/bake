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

#include <stdlib.h>
#include <string.h>

#include "private.h"

protecc_node_t* protecc_node_new(protecc_node_type_t type) {
    protecc_node_t* node = calloc(1, sizeof(protecc_node_t));
    if (!node) {
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
    if (!node) {
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
    if (!parent || !child) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }
    
    // Check if we need to expand capacity
    if (parent->num_children >= parent->capacity_children) {
        size_t new_capacity = parent->capacity_children == 0 ? 4 : parent->capacity_children * 2;
        protecc_node_t** new_children = realloc(
            parent->children,
            new_capacity * sizeof(protecc_node_t*)
        );
        if (!new_children) {
            return PROTECC_ERROR_OUT_OF_MEMORY;
        }
        parent->children = new_children;
        parent->capacity_children = new_capacity;
    }
    
    parent->children[parent->num_children++] = child;
    return PROTECC_OK;
}

void protecc_charset_set(protecc_charset_t* charset, unsigned char c) {
    if (!charset) {
        return;
    }
    charset->chars[c / 8] |= (1 << (c % 8));
}

bool protecc_charset_contains(const protecc_charset_t* charset, unsigned char c) {
    if (!charset) {
        return false;
    }
    return (charset->chars[c / 8] & (1 << (c % 8))) != 0;
}

void protecc_charset_set_range(protecc_charset_t* charset, char start, char end) {
    if (!charset) {
        return;
    }
    // Handle unsigned char properly to avoid overflow at 255
    unsigned char ustart = (unsigned char)start;
    unsigned char uend = (unsigned char)end;
    for (unsigned int c = ustart; c <= uend; c++) {
        protecc_charset_set(charset, (unsigned char)c);
        if (c == 255) break; // Prevent overflow
    }
}
