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

#ifndef __PROTECC_BPF_H__
#define __PROTECC_BPF_H__

#include <protecc/profile.h>

// BPF includes
#include <bpf/bpf_helpers.h>

#define PROTECC_INVALID_EDGE_INDEX 0xFFFFFFFFu

#ifndef PROTECC_FLAG_CASE_INSENSITIVE
#define PROTECC_FLAG_CASE_INSENSITIVE (1u << 0)
#endif

#define PROTECC_BPF_MAX_PATH         256u
#define PROTECC_BPF_MAX_STACK        128u
#define PROTECC_BPF_MAX_STEPS        4096u
#define PROTECC_BPF_MAX_CHILDREN     32u
#define PROTECC_BPF_MAX_PROFILE_SIZE (65536u - 4u)

typedef struct {
    __u32 node_index;
    __u32 pos;
} protecc_bpf_state_t;

static __always_inline uint8_t __tolower(uint8_t c) {
    return (c >= 'A' && c <= 'Z') ? (uint8_t)(c + 32) : c;
}

static __always_inline bool __charset_contains(const uint8_t* charset, uint8_t c) {
    return (charset[c / 8] & (uint8_t)(1u << (c & 7u))) != 0u;
}

static __always_inline bool __char_matches(const protecc_profile_node_t* node, uint8_t c, __u32 flags) {
    if (flags & PROTECC_FLAG_CASE_INSENSITIVE) {
        c = __tolower(c);
    }

    switch (node->type) {
        case 0: /* NODE_LITERAL */
            return c == node->data.literal;
        case 1: /* NODE_WILDCARD_SINGLE */
            return c != '\0';
        case 4: /* NODE_CHARSET */
            return __charset_contains(node->data.charset, c);
        case 5: /* NODE_RANGE */
            return c >= node->data.range.start && c <= node->data.range.end;
        default:
            return false;
    }
}

static __always_inline bool __validate_profile_header(const protecc_profile_header_t* header) {
    if (header == NULL) {
        return false;
    }
    
    if (header->magic != PROTECC_PROFILE_MAGIC || header->version != PROTECC_PROFILE_VERSION) {
        return false;
    }
    
    if (header->num_nodes == 0 || header->root_index >= header->num_nodes) {
        return false;
    }

    return true;
}

static __always_inline protecc_profile_node_t* __get_node(const __u8 profile[PROTECC_BPF_MAX_PROFILE_SIZE], __u32 nodeIndex) {
    const protecc_profile_header_t* header = (const protecc_profile_header_t*)profile;
    protecc_profile_node_t*         node;

    if (nodeIndex >= header->num_nodes) {
        return NULL;
    }

    node = (protecc_profile_node_t*)(&profile[sizeof(protecc_profile_header_t) + (nodeIndex * sizeof(protecc_profile_node_t))]);
    if ((const __u8*)node < (const __u8*)profile || (const __u8*)(node + 1) > ((const __u8*)profile + PROTECC_BPF_MAX_PROFILE_SIZE)) {
        return NULL;
    }
    return node;
}

static __always_inline __u32 __get_edge_value(
    const __u8 profile[PROTECC_BPF_MAX_PROFILE_SIZE],
    __u32      edgeIndex) {
    const protecc_profile_header_t* header = (const protecc_profile_header_t*)profile;
    const __u32*                    edge;

    if (edgeIndex >= header->num_edges) {
        return PROTECC_INVALID_EDGE_INDEX;
    }

    edge = ((const __u32*)(&profile[sizeof(protecc_profile_header_t) + (header->num_nodes * sizeof(protecc_profile_node_t))])) + edgeIndex;
    if (edge < (const __u32*)profile || (edge + 1) >= (const __u32*)((const __u8*)profile + PROTECC_BPF_MAX_PROFILE_SIZE)) {
        return PROTECC_INVALID_EDGE_INDEX;
    }
    return *edge;
}

struct __step_context {
    protecc_bpf_state_t* stack;
    __u8                 stack_index;
    const __u8*          profile;
    const char*          path;
    __u32                path_length;

    bool                 match;
};

static long __protecc_step_handler(__u64 index, void *ctx) {
    struct __step_context*          context = (struct __step_context*)ctx;
    const protecc_profile_header_t* header = (const protecc_profile_header_t*)context->profile;
    const protecc_profile_node_t*   node;
    protecc_bpf_state_t*            state;
    __u32                           childCount;

    // ensure the stack is valid
    if (context->stack_index <= 0 || context->stack_index >= PROTECC_BPF_MAX_STACK) {
        return 1;
    }

    state = &context->stack[(--context->stack_index) & (PROTECC_BPF_MAX_STACK - 1)];
    node = __get_node(context->profile, state->node_index);
    if (node == NULL) {
        return 0;
    }

    if (state->pos == context->path_length && node->is_terminal) {
        context->match = true;
        return 1;
    }

    if (node->child_count == 0 || node->child_count > PROTECC_BPF_MAX_CHILDREN) {
        return 0;
    }

    __u32 childStart = node->child_start;
    __u32 i;
    
    bpf_for (i, 0, PROTECC_BPF_MAX_CHILDREN) {
        const protecc_profile_node_t* child;
        uint8_t                       modifier;
        __u32                         childIndex;

        if (i >= node->child_count) {
            break;
        }

        childIndex = __get_edge_value(context->profile, childStart + i);
        if (childIndex == PROTECC_INVALID_EDGE_INDEX || childIndex >= header->num_nodes) {
            continue;
        }

        child = __get_node(context->profile, childIndex);
        if (child == NULL) {
            continue;
        }
        
        modifier = child->modifier;
        if (modifier != 0) {
            int   hasNext = (i + 1u) < node->child_count;
            __u32 nextIndex = hasNext ? __get_edge_value(context->profile, childStart + i + 1u) : 0;

            if (modifier == 1) { /* MODIFIER_OPTIONAL */
                if (state->pos < context->path_length &&
                    __char_matches(child, (uint8_t)context->path[state->pos], header->flags)) {
                    if (hasNext && nextIndex < header->num_nodes && context->stack_index < PROTECC_BPF_MAX_STACK) {
                        context->stack[context->stack_index++] = (protecc_bpf_state_t){ nextIndex, state->pos + 1u };
                    } else if (!hasNext && child->is_terminal && state->pos + 1u == context->path_length) {
                        context->match = true;
                        return 1;
                    }
                }
                if (hasNext && nextIndex < header->num_nodes && context->stack_index < PROTECC_BPF_MAX_STACK) {
                    context->stack[context->stack_index++] = (protecc_bpf_state_t){ nextIndex, state->pos };
                } else if (!hasNext && child->is_terminal && state->pos == context->path_length) {
                    context->match = true;
                    return 1;
                }
            } else if (modifier == 2 || modifier == 3) { /* + or * */
                __u32 pos = state->pos;
                __u32 k;
                
                if (modifier == 2) {
                    if (pos >= context->path_length ||
                        !__char_matches(child, (uint8_t)context->path[pos], header->flags)) {
                        continue;
                    }
                    pos++;
                }

                bpf_for (k, 0, PROTECC_BPF_MAX_PATH + 1) {
                    if (pos > context->path_length) {
                        break;
                    }
                    
                    if (hasNext && nextIndex < header->num_nodes && context->stack_index < PROTECC_BPF_MAX_STACK) {
                        context->stack[context->stack_index++] = (protecc_bpf_state_t){ nextIndex, pos };
                    } else if (!hasNext && child->is_terminal && pos == context->path_length) {
                        context->match = true;
                        return 1;
                    }

                    if (pos >= context->path_length) {
                        break;
                    }
                    if (!__char_matches(child, (uint8_t)context->path[pos], header->flags)) {
                        break;
                    }
                    pos++;
                }
            }

            continue;
        }

        switch (child->type) {
            case 3: { /* NODE_WILDCARD_RECURSIVE */
                __u32 tryPos = state->pos;
                __u32 k;
                
                bpf_for (k, 0, PROTECC_BPF_MAX_PATH + 1) {
                    if (tryPos > context->path_length) {
                        break;
                    }
                    if (context->stack_index < PROTECC_BPF_MAX_STACK) {
                        context->stack[context->stack_index++] = (protecc_bpf_state_t){ childIndex, tryPos };
                    }
                    tryPos++;
                }
                break;
            }
            case 2: { /* NODE_WILDCARD_MULTI */
                __u32 tryPos = state->pos;
                __u32 k;
                
                bpf_for (k, 0, PROTECC_BPF_MAX_PATH + 1) {
                    if (tryPos > context->path_length) {
                        break;
                    }
                    if (context->stack_index < PROTECC_BPF_MAX_STACK) {
                        context->stack[context->stack_index++] = (protecc_bpf_state_t){ childIndex, tryPos };
                    }
                    if (tryPos < context->path_length && context->path[tryPos] == '/') {
                        break;
                    }
                    tryPos++;
                }
                break;
            }
            case 0: /* NODE_LITERAL */
            case 1: /* NODE_WILDCARD_SINGLE */
            case 4: /* NODE_CHARSET */
            case 5: /* NODE_RANGE */
                if (state->pos < context->path_length &&
                    __char_matches(child, (uint8_t)context->path[state->pos], header->flags)) {
                    if (context->stack_index < PROTECC_BPF_MAX_STACK) {
                        context->stack[context->stack_index++] = (protecc_bpf_state_t){ childIndex, state->pos + 1u };
                    }
                }
                break;
            default:
                break;
        }
    }
    return 0;
}

static __always_inline bool protecc_bpf_match(
    protecc_bpf_state_t stack[PROTECC_BPF_MAX_STACK],
    const __u8          profile[PROTECC_BPF_MAX_PROFILE_SIZE],
    const char*         path,
    __u32               pathLength)
{
    const protecc_profile_header_t* header = (const protecc_profile_header_t*)profile;
    struct __step_context           context;

    if (profile == NULL || path == NULL) {
        return false;
    }

    if (__validate_profile_header(header) == false) {
        return false;
    }

    context.profile = profile;
    context.stack = stack;
    context.stack_index = 0;
    context.path = path;
    context.path_length = pathLength;
    context.match = false;

    // initialize the stack with the root node
    context.stack[context.stack_index++] = (protecc_bpf_state_t){ header->root_index, 0 };
    
    // run the matching loop
    bpf_loop(PROTECC_BPF_MAX_STEPS, __protecc_step_handler, &context, 0);

    // return the result
    return context.match;
}

#endif // !__PROTECC_BPF_H__
