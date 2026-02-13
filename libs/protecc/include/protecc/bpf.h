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

#ifndef PROTECC_FLAG_CASE_INSENSITIVE
#define PROTECC_FLAG_CASE_INSENSITIVE (1u << 0)
#endif

#ifndef PROTECC_BPF_MAX_PATH
#define PROTECC_BPF_MAX_PATH 256u
#endif

#ifndef PROTECC_BPF_MAX_STACK
#define PROTECC_BPF_MAX_STACK 128u
#endif

#ifndef PROTECC_BPF_MAX_STEPS
#define PROTECC_BPF_MAX_STEPS 4096u
#endif

#ifndef PROTECC_BPF_MAX_CHILDREN
#define PROTECC_BPF_MAX_CHILDREN 32u
#endif

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

static __always_inline bool __validate_profile(const protecc_profile_header_t* header) {
    if (header->magic != PROTECC_PROFILE_MAGIC || header->version != PROTECC_PROFILE_VERSION) {
        return false;
    }
    if (header->num_nodes == 0 || header->root_index >= header->num_nodes) {
        return false;
    }
    return true;
}

static __always_inline bool protecc_bpf_match(
    const void* profile,
    const char* path,
    __u32       pathLength)
{
    const protecc_profile_header_t* header = (const protecc_profile_header_t*)profile;
    const uint8_t*                  base = (const uint8_t*)profile;
    const protecc_profile_node_t*   nodes;
    const __u32*                    edges;
    protecc_bpf_state_t             stack[PROTECC_BPF_MAX_STACK];
    __u32                           sp = 0, steps;

    if (profile == NULL || path == NULL) {
        return false;
    }

    if (__validate_profile(header) == false) {
        return false;
    }

    nodes = (const protecc_profile_node_t*)(base + sizeof(protecc_profile_header_t));
    edges = (const __u32*)(base + sizeof(protecc_profile_header_t)
                          + (uint64_t)header->num_nodes * sizeof(protecc_profile_node_t));

    stack[sp++] = (protecc_bpf_state_t){ header->root_index, 0 };

    bpf_for (steps, 0, PROTECC_BPF_MAX_STEPS) {
        if (sp == 0) {
            break;
        }

        protecc_bpf_state_t state = stack[--sp];
        const protecc_profile_node_t* node = &nodes[state.node_index];

        if (state.pos == pathLength && node->is_terminal) {
            return true;
        }
        if (node->child_count == 0 || node->child_count > PROTECC_BPF_MAX_CHILDREN) {
            continue;
        }

        __u32 childStart = node->child_start;
        __u32 i;
        bpf_for (i, 0, node->child_count) {
            __u32 childIndex = edges[childStart + i];
            if (childIndex >= header->num_nodes) {
                continue;
            }

            const protecc_profile_node_t* child = &nodes[childIndex];
            uint8_t modifier = child->modifier;

            if (modifier != 0) {
                int   hasNext = (i + 1u) < node->child_count;
                __u32 nextIndex = hasNext ? edges[childStart + i + 1u] : 0;

                if (modifier == 1) { /* MODIFIER_OPTIONAL */
                    if (state.pos < pathLength &&
                        __char_matches(child, (uint8_t)path[state.pos], header->flags)) {
                        if (hasNext && nextIndex < header->num_nodes && sp < PROTECC_BPF_MAX_STACK) {
                            stack[sp++] = (protecc_bpf_state_t){ nextIndex, state.pos + 1u };
                        } else if (!hasNext && child->is_terminal && state.pos + 1u == pathLength) {
                            return true;
                        }
                    }
                    if (hasNext && nextIndex < header->num_nodes && sp < PROTECC_BPF_MAX_STACK) {
                        stack[sp++] = (protecc_bpf_state_t){ nextIndex, state.pos };
                    } else if (!hasNext && child->is_terminal && state.pos == pathLength) {
                        return true;
                    }
                } else if (modifier == 2 || modifier == 3) { /* + or * */
                    __u32 pos = state.pos;
                    __u32 k;
                    
                    if (modifier == 2) {
                        if (pos >= pathLength ||
                            !__char_matches(child, (uint8_t)path[pos], header->flags)) {
                            continue;
                        }
                        pos++;
                    }

                    bpf_for (k, 0, PROTECC_BPF_MAX_PATH + 1) {
                        if (pos > pathLength) {
                            break;
                        }
                        
                        if (hasNext && nextIndex < header->num_nodes && sp < PROTECC_BPF_MAX_STACK) {
                            stack[sp++] = (protecc_bpf_state_t){ nextIndex, pos };
                        } else if (!hasNext && child->is_terminal && pos == pathLength) {
                            return true;
                        }

                        if (pos >= pathLength) {
                            break;
                        }
                        if (!__char_matches(child, (uint8_t)path[pos], header->flags)) {
                            break;
                        }
                        pos++;
                    }
                }

                continue;
            }

            switch (child->type) {
                case 3: { /* NODE_WILDCARD_RECURSIVE */
                    __u32 tryPos = state.pos;
                    __u32 k;
                    
                    bpf_for (k, 0, PROTECC_BPF_MAX_PATH + 1) {
                        if (tryPos > pathLength) {
                            break;
                        }
                        if (sp < PROTECC_BPF_MAX_STACK) {
                            stack[sp++] = (protecc_bpf_state_t){ childIndex, tryPos };
                        }
                        tryPos++;
                    }
                    break;
                }
                case 2: { /* NODE_WILDCARD_MULTI */
                    __u32 tryPos = state.pos;
                    __u32 k;
                    
                    bpf_for (k, 0, PROTECC_BPF_MAX_PATH + 1) {
                        if (tryPos > pathLength) {
                            break;
                        }
                        if (sp < PROTECC_BPF_MAX_STACK) {
                            stack[sp++] = (protecc_bpf_state_t){ childIndex, tryPos };
                        }
                        if (tryPos < pathLength && path[tryPos] == '/') {
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
                    if (state.pos < pathLength &&
                        __char_matches(child, (uint8_t)path[state.pos], header->flags)) {
                        if (sp < PROTECC_BPF_MAX_STACK) {
                            stack[sp++] = (protecc_bpf_state_t){ childIndex, state.pos + 1u };
                        }
                    }
                    break;
                default:
                    break;
            }
        }
    }

    return false;
}

#endif // !__PROTECC_BPF_H__
