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

#include <string.h>
#include <ctype.h>
#include <stdlib.h>

#include "private.h"

static bool char_matches_node(
    const protecc_node_t* node,
    char c,
    uint32_t flags
) {
    bool case_insensitive = (flags & PROTECC_FLAG_CASE_INSENSITIVE) != 0;
    
    if (case_insensitive) {
        c = tolower(c);
    }
    
    switch (node->type) {
        case NODE_LITERAL:
            return c == node->data.literal;
        
        case NODE_WILDCARD_SINGLE:
            return c != '\0';
        
        case NODE_CHARSET:
            return protecc_charset_contains(&node->data.charset, (unsigned char)c);
        
        case NODE_RANGE:
            if (case_insensitive) {
                return (c >= tolower(node->data.range.start) && 
                        c <= tolower(node->data.range.end));
            }
            return (c >= node->data.range.start && c <= node->data.range.end);
        
        default:
            return false;
    }
}

/**
 * @brief Match with modifier support
 */
typedef struct {
    const protecc_node_t* node;
    size_t                pos;
} matcher_frame_t;

static bool matcher_stack_push(
    matcher_frame_t** stack,
    size_t*           stack_size,
    size_t*           stack_capacity,
    const protecc_node_t* node,
    size_t            pos)
{
    matcher_frame_t* resized;
    size_t           new_capacity;

    if (*stack_size >= *stack_capacity) {
        new_capacity = (*stack_capacity == 0) ? 64 : (*stack_capacity * 2);
        resized = realloc(*stack, new_capacity * sizeof(matcher_frame_t));
        if (!resized) {
            return false;
        }
        *stack = resized;
        *stack_capacity = new_capacity;
    }

    (*stack)[*stack_size].node = node;
    (*stack)[*stack_size].pos = pos;
    (*stack_size)++;
    return true;
}

bool protecc_match_internal(
    const protecc_node_t* node,
    const char* path,
    size_t path_len,
    size_t pos,
    uint32_t flags
) {
    matcher_frame_t* stack = NULL;
    size_t           stack_size = 0;
    size_t           stack_capacity = 0;

    if (!node) {
        return false;
    }

    if (!matcher_stack_push(&stack, &stack_size, &stack_capacity, node, pos)) {
        return false;
    }

    while (stack_size > 0) {
        matcher_frame_t       frame;
        const protecc_node_t* current;

        stack_size--;
        frame = stack[stack_size];
        current = frame.node;

        if (frame.pos == path_len && current->is_terminal) {
            free(stack);
            return true;
        }

        if (current->num_children == 0) {
            continue;
        }

        for (size_t i = 0; i < current->num_children; i++) {
            const protecc_node_t* child = current->children[i];

            if (child->modifier != MODIFIER_NONE) {
                const protecc_node_t* next = (i + 1 < current->num_children) ? current->children[i + 1] : NULL;

                if (child->modifier == MODIFIER_OPTIONAL) {
                    if (next) {
                        if (!matcher_stack_push(&stack, &stack_size, &stack_capacity, next, frame.pos)) {
                            free(stack);
                            return false;
                        }
                    } else if (child->is_terminal && frame.pos == path_len) {
                        free(stack);
                        return true;
                    }

                    if (frame.pos < path_len && char_matches_node(child, path[frame.pos], flags)) {
                        if (next) {
                            if (!matcher_stack_push(&stack, &stack_size, &stack_capacity, next, frame.pos + 1)) {
                                free(stack);
                                return false;
                            }
                        } else if (child->is_terminal && frame.pos + 1 == path_len) {
                            free(stack);
                            return true;
                        }
                    }
                    continue;
                }

                if (child->modifier == MODIFIER_ONE_OR_MORE || child->modifier == MODIFIER_ZERO_OR_MORE) {
                    size_t k = frame.pos;

                    if (child->modifier == MODIFIER_ONE_OR_MORE) {
                        if (k >= path_len || !char_matches_node(child, path[k], flags)) {
                            continue;
                        }
                        k++;
                    }

                    while (k <= path_len) {
                        if (next) {
                            if (!matcher_stack_push(&stack, &stack_size, &stack_capacity, next, k)) {
                                free(stack);
                                return false;
                            }
                        } else if (child->is_terminal && k == path_len) {
                            free(stack);
                            return true;
                        }

                        if (k >= path_len || !char_matches_node(child, path[k], flags)) {
                            break;
                        }
                        k++;
                    }
                    continue;
                }

                continue;
            }

            switch (child->type) {
                case NODE_WILDCARD_RECURSIVE:
                    for (size_t try_pos = frame.pos; try_pos <= path_len; try_pos++) {
                        if (!matcher_stack_push(&stack, &stack_size, &stack_capacity, child, try_pos)) {
                            free(stack);
                            return false;
                        }
                    }
                    break;

                case NODE_WILDCARD_MULTI: {
                    size_t try_pos = frame.pos;
                    while (try_pos <= path_len) {
                        if (!matcher_stack_push(&stack, &stack_size, &stack_capacity, child, try_pos)) {
                            free(stack);
                            return false;
                        }
                        if (try_pos < path_len && path[try_pos] == '/') {
                            break;
                        }
                        try_pos++;
                    }
                    break;
                }

                case NODE_WILDCARD_SINGLE:
                case NODE_LITERAL:
                case NODE_CHARSET:
                case NODE_RANGE:
                    if (frame.pos < path_len && char_matches_node(child, path[frame.pos], flags)) {
                        if (!matcher_stack_push(&stack, &stack_size, &stack_capacity, child, frame.pos + 1)) {
                            free(stack);
                            return false;
                        }
                    }
                    break;

                default:
                    break;
            }
        }
    }

    free(stack);
    return false;
}
