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

#include "../private.h"

static bool __char_matches_node(const protecc_node_t* node, char c, uint32_t flags)
{
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
    size_t                depth;
} matcher_frame_t;

struct __matcher_state {
    bool                 found;
    size_t               best_depth;
    protecc_permission_t best_permissions;

    matcher_frame_t*     stack;
    size_t               stack_size;
    size_t               stack_capacity;
};

static bool __matcher_stack_push(
    struct __matcher_state* state,
    const protecc_node_t*   node,
    size_t                  pos,
    size_t                  depth)
{
    matcher_frame_t* resized;
    size_t           newCapacity;

    if (state->stack_size >= state->stack_capacity) {
        newCapacity = (state->stack_capacity == 0) ? 64 : (state->stack_capacity * 2);
        resized = realloc(state->stack, newCapacity * sizeof(matcher_frame_t));
        if (!resized) {
            return false;
        }
        state->stack = resized;
        state->stack_capacity = newCapacity;
    }

    state->stack[state->stack_size].node = node;
    state->stack[state->stack_size].pos = pos;
    state->stack[state->stack_size].depth = depth;
    state->stack_size++;
    return true;
}

static void __matcher_state_update(
    struct __matcher_state* state,
    size_t                  depth,
    protecc_permission_t    perms)
{
    if (!state->found || depth > state->best_depth) {
        state->best_depth = depth;
        state->best_permissions = perms;
        state->found = true;
    } else if (depth == state->best_depth) {
        state->best_permissions |= perms;
    }
}

bool __matcher_trie(
    const protecc_node_t* node,
    const char*           path,
    size_t                pos,
    uint32_t              flags,
    protecc_permission_t  requiredPermissions)
{
    size_t                 pathLength = strlen(path);
    struct __matcher_state state = { 
        .found = false, 
        .best_depth = 0, 
        .best_permissions = 0,

        .stack = NULL,
        .stack_size = 0,
        .stack_capacity = 0
    };

    if (node == NULL || pathLength == 0) {
        return false;
    }

    if (!__matcher_stack_push(&state, node, pos, 0)) {
        return false;
    }

    while (state.stack_size > 0) {
        matcher_frame_t       frame;
        const protecc_node_t* current;

        state.stack_size--;
        frame = state.stack[state.stack_size];
        current = frame.node;

        if (frame.pos == pathLength && current->is_terminal) {
            __matcher_state_update(&state, frame.depth, current->perms);
        }

        if (current->num_children == 0) {
            continue;
        }

        for (size_t i = 0; i < current->num_children; i++) {
            const protecc_node_t* child = current->children[i];
            size_t child_depth = frame.depth + 1u;

            if (child->modifier != MODIFIER_NONE) {
                const protecc_node_t* next = (i + 1 < current->num_children) ? current->children[i + 1] : NULL;
                size_t next_depth = child_depth;

                if (child->modifier == MODIFIER_OPTIONAL) {
                    if (next) {
                        if (!__matcher_stack_push(&state, next, frame.pos, next_depth)) {
                            state.found = false;
                            goto exit;
                        }
                    } else if (child->is_terminal && frame.pos == pathLength) {
                        __matcher_state_update(&state, child_depth, child->perms);
                    }

                    if (frame.pos < pathLength && __char_matches_node(child, path[frame.pos], flags)) {
                        if (next) {
                            if (!__matcher_stack_push(&state, next, frame.pos + 1, next_depth)) {
                                state.found = false;
                                goto exit;
                            }
                        } else if (child->is_terminal && frame.pos + 1 == pathLength) {
                            __matcher_state_update(&state, child_depth, child->perms);
                        }
                    }
                    continue;
                }

                if (child->modifier == MODIFIER_ONE_OR_MORE || child->modifier == MODIFIER_ZERO_OR_MORE) {
                    size_t k = frame.pos;

                    if (child->modifier == MODIFIER_ONE_OR_MORE) {
                        if (k >= pathLength || !__char_matches_node(child, path[k], flags)) {
                            continue;
                        }
                        k++;
                    }

                    while (k <= pathLength) {
                        if (next) {
                            if (!__matcher_stack_push(&state, next, k, next_depth)) {
                                state.found = false;
                                goto exit;
                            }
                        } else if (child->is_terminal && k == pathLength) {
                            __matcher_state_update(&state, child_depth, child->perms);
                        }

                        if (k >= pathLength || !__char_matches_node(child, path[k], flags)) {
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
                    for (size_t try_pos = frame.pos; try_pos <= pathLength; try_pos++) {
                        if (!__matcher_stack_push(&state, child, try_pos, child_depth)) {
                            state.found = false;
                            goto exit;
                        }
                    }
                    break;

                case NODE_WILDCARD_MULTI: {
                    size_t try_pos = frame.pos;
                    while (try_pos <= pathLength) {
                        if (!__matcher_stack_push(&state, child, try_pos, child_depth)) {
                            state.found = false;
                            goto exit;
                        }
                        if (try_pos < pathLength && path[try_pos] == '/') {
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
                    if (frame.pos < pathLength && __char_matches_node(child, path[frame.pos], flags)) {
                        if (!__matcher_stack_push(&state, child, frame.pos + 1, child_depth)) {
                            state.found = false;
                            goto exit;
                        }
                    }
                    break;

                default:
                    break;
            }
        }
    }

exit:
    free(state.stack);

    // Check if the best permissions found satisfy the required permissions
    if (state.found) {
        return (state.best_permissions & requiredPermissions) == requiredPermissions;
    }
    return state.found;
}
