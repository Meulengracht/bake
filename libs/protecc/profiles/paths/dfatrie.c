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

static void protecc_collect_nodes(
    const protecc_node_t* node,
    const protecc_node_t** nodes,
    size_t* index
) {
    if (node == NULL) {
        return;
    }

    nodes[(*index)++] = node;
    for (size_t i = 0; i < node->num_children; i++) {
        protecc_collect_nodes(node->children[i], nodes, index);
    }
}

static size_t protecc_find_node_index(
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

static size_t __bitset_words(size_t bit_count) {
    return (bit_count + 63u) / 64u;
}

static void __bitset_set(uint64_t* bits, size_t index) {
    bits[index >> 6] |= (1ull << (index & 63u));
}

static bool __bitset_test(const uint64_t* bits, size_t index) {
    return (bits[index >> 6] & (1ull << (index & 63u))) != 0;
}

static bool __char_matches_node(const protecc_node_t* node, unsigned char c, uint32_t flags) {
    bool case_insensitive = (flags & PROTECC_FLAG_CASE_INSENSITIVE) != 0;
    char ch = (char)c;

    if (case_insensitive) {
        ch = (char)tolower((unsigned char)ch);
    }

    switch (node->type) {
        case NODE_LITERAL:
            return ch == node->data.literal;
        case NODE_WILDCARD_SINGLE:
            return ch != '\0';
        case NODE_WILDCARD_MULTI:
            return ch != '/';
        case NODE_WILDCARD_RECURSIVE:
            return true;
        case NODE_CHARSET:
            return protecc_charset_contains(&node->data.charset, (unsigned char)ch);
        case NODE_RANGE:
            return ch >= node->data.range.start && ch <= node->data.range.end;
        default:
            return false;
    }
}

static bool __state_set_best_perms(
    const uint64_t*               set_bits,
    const protecc_node_t* const*  nodes,
    const uint16_t*               node_depths,
    size_t                        node_count,
    uint32_t*                     perms_out)
{
    bool found = false;
    uint16_t best_depth = 0;
    uint32_t perms = 0;

    if (!perms_out) {
        return false;
    }

    for (size_t i = 0; i < node_count; i++) {
        if (!__bitset_test(set_bits, i) || !nodes[i]->is_terminal) {
            continue;
        }

        if (!found || node_depths[i] > best_depth) {
            best_depth = node_depths[i];
            perms = (uint32_t)nodes[i]->perms;
            found = true;
        } else if (node_depths[i] == best_depth) {
            perms |= (uint32_t)nodes[i]->perms;
        }
    }

    *perms_out = perms;
    return found;
}

static void __compute_node_depths(
    const protecc_node_t*         node,
    const protecc_node_t* const*  nodes,
    size_t                        node_count,
    uint16_t*                     depths,
    uint16_t                      depth)
{
    size_t index = protecc_find_node_index(nodes, node_count, node);

    if (index == SIZE_MAX || depths[index] <= depth) {
        return;
    }

    depths[index] = depth;
    for (size_t i = 0; i < node->num_children; i++) {
        __compute_node_depths(node->children[i], nodes, node_count, depths, (uint16_t)(depth + 1u));
    }
}

static void __epsilon_closure(
    uint64_t*                     set_bits,
    const protecc_node_t* const*  nodes,
    size_t                        node_count,
    const size_t*                 next_sibling)
{
    bool changed = true;

    while (changed) {
        changed = false;
        for (size_t i = 0; i < node_count; i++) {
            const protecc_node_t* node;

            if (!__bitset_test(set_bits, i)) {
                continue;
            }

            node = nodes[i];
            for (size_t c = 0; c < node->num_children; c++) {
                const protecc_node_t* child = node->children[c];
                size_t child_index = protecc_find_node_index(nodes, node_count, child);
                size_t next_index = SIZE_MAX;

                if (child_index == SIZE_MAX) {
                    continue;
                }

                if (c + 1u < node->num_children) {
                    next_index = protecc_find_node_index(nodes, node_count, node->children[c + 1u]);
                }

                if (child->modifier == MODIFIER_OPTIONAL || child->modifier == MODIFIER_ZERO_OR_MORE) {
                    if (next_index != SIZE_MAX && !__bitset_test(set_bits, next_index)) {
                        __bitset_set(set_bits, next_index);
                        changed = true;
                    } else if (next_index == SIZE_MAX && !__bitset_test(set_bits, child_index)) {
                        __bitset_set(set_bits, child_index);
                        changed = true;
                    }
                }

                if (child->type == NODE_WILDCARD_MULTI || child->type == NODE_WILDCARD_RECURSIVE) {
                    if (!__bitset_test(set_bits, child_index)) {
                        __bitset_set(set_bits, child_index);
                        changed = true;
                    }
                }
            }

            if (node->modifier == MODIFIER_ONE_OR_MORE || node->modifier == MODIFIER_ZERO_OR_MORE ||
                node->modifier == MODIFIER_OPTIONAL) {
                size_t next_index = next_sibling[i];
                if (next_index != SIZE_MAX && !__bitset_test(set_bits, next_index)) {
                    __bitset_set(set_bits, next_index);
                    changed = true;
                }
            }
        }
    }
}

static ptrdiff_t __dfa_find_state(
    const uint64_t* states,
    size_t          state_count,
    size_t          words_per_state,
    const uint64_t* candidate)
{
    for (size_t i = 0; i < state_count; i++) {
        const uint64_t* state = states + (i * words_per_state);
        if (memcmp(state, candidate, words_per_state * sizeof(uint64_t)) == 0) {
            return (ptrdiff_t)i;
        }
    }
    return -1;
}

struct __dfa_builder_state {
    size_t                 state_capacity;
    size_t                 state_count;
    uint64_t*              state_sets;
    uint64_t*              scratch_set;

    uint32_t*              transitions;
    uint32_t*              accept;
    uint32_t*              perms;
    uint16_t*              node_depths;
};

static void __dfa_builder_state_cleanup(struct __dfa_builder_state* state) {
    free(state->transitions);
    free(state->accept);
    free(state->perms);
    free(state->node_depths);
}

static int __dfa_builder_state_add_state(struct __dfa_builder_state* state, size_t maxStateCount, size_t wordsPerState) {
    if (state->state_count >= state->state_capacity) {
        size_t    newCapacity = state->state_capacity * 2u;
        uint64_t* newStates;
        uint32_t* newTransitions;

        if (newCapacity > maxStateCount) {
            newCapacity = maxStateCount;
        }
        if (newCapacity <= state->state_capacity) {
            return PROTECC_ERROR_COMPILE_FAILED;
        }

        newStates = realloc(state->state_sets, newCapacity * wordsPerState * sizeof(uint64_t));
        if (!newStates) {
            return PROTECC_ERROR_OUT_OF_MEMORY;
        }

        state->state_sets = newStates;
        memset(
            state->state_sets + (state->state_capacity * wordsPerState),
            0,
            (newCapacity - state->state_capacity) * wordsPerState * sizeof(uint64_t)
        );

        newTransitions = realloc(state->transitions, newCapacity * 256u * sizeof(uint32_t));
        if (!newTransitions) {
            return PROTECC_ERROR_OUT_OF_MEMORY;
        }
        
        state->transitions = newTransitions;
        memset(
            state->transitions + (state->state_capacity * 256u),
            0,
            (newCapacity - state->state_capacity) * 256u * sizeof(uint32_t)
        );
        state->state_capacity = newCapacity;
    }

    memcpy(
        state->state_sets + (state->state_count * wordsPerState),
        state->scratch_set,
        wordsPerState * sizeof(uint64_t)
    );
    return 0;
}

protecc_error_t protecc_dfa_from_trie(protecc_profile_t* profile)
{
    const protecc_node_t**     nodes = NULL;
    struct __dfa_builder_state state = {
        .state_capacity = 16,
        .state_count = 0,
        .state_sets = NULL,
        .scratch_set = NULL,
        .transitions = NULL,
        .accept = NULL,
        .perms = NULL,
        .node_depths = NULL
    };
    size_t*                next_sibling = NULL;
    uint16_t*              node_depths = NULL;
    size_t                 index = 0;
    size_t                 words_per_state;
    size_t                 state_capacity;
    size_t                 state_count;
    size_t                 queue_index;
    size_t                 num_nodes = 0;
    size_t                 max_depth = 0;
    size_t                 num_edges = 0;

    if (profile == NULL || profile->root == NULL) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    protecc_node_collect_stats(profile->root, 0, &num_nodes, &max_depth, &num_edges);
    if (num_nodes == 0) {
        return PROTECC_ERROR_COMPILE_FAILED;
    }

    nodes = calloc(num_nodes, sizeof(*nodes));
    next_sibling = malloc(num_nodes * sizeof(*next_sibling));
    if (!nodes || !next_sibling) {
        free(next_sibling);
        return PROTECC_ERROR_OUT_OF_MEMORY;
    }

    for (size_t i = 0; i < num_nodes; i++) {
        next_sibling[i] = SIZE_MAX;
    }

    protecc_collect_nodes(profile->root, nodes, &index);
    if (index != num_nodes) {
        free(next_sibling);
        free(nodes);
        return PROTECC_ERROR_COMPILE_FAILED;
    }

    node_depths = malloc(num_nodes * sizeof(*node_depths));
    if (!node_depths) {
        free(next_sibling);
        free(nodes);
        return PROTECC_ERROR_OUT_OF_MEMORY;
    }

    for (size_t i = 0; i < num_nodes; i++) {
        node_depths[i] = UINT16_MAX;
    }
    __compute_node_depths(profile->root, nodes, num_nodes, node_depths, 0);

    for (size_t i = 0; i < num_nodes; i++) {
        const protecc_node_t* node = nodes[i];
        for (size_t c = 0; c < node->num_children; c++) {
            size_t child_idx = protecc_find_node_index(nodes, num_nodes, node->children[c]);
            if (child_idx == SIZE_MAX) {
                free(next_sibling);
                free(node_depths);
                free(nodes);
                return PROTECC_ERROR_COMPILE_FAILED;
            }

            if (c + 1u < node->num_children) {
                size_t next_idx = protecc_find_node_index(nodes, num_nodes, node->children[c + 1u]);
                if (next_idx == SIZE_MAX) {
                    free(next_sibling);
                    free(node_depths);
                    free(nodes);
                    return PROTECC_ERROR_COMPILE_FAILED;
                }
                next_sibling[child_idx] = next_idx;
            }
        }
    }

    words_per_state = __bitset_words(num_nodes);
    state_capacity = 16;
    state_count = 0;
    queue_index = 0;

    state_sets = calloc(state_capacity * words_per_state, sizeof(uint64_t));
    scratch_set = calloc(words_per_state, sizeof(uint64_t));
    if (!state_sets || !scratch_set) {
        free(scratch_set);
        free(state_sets);
        free(node_depths);
        free(nodes);
        return PROTECC_ERROR_OUT_OF_MEMORY;
    }

    __bitset_set(state_sets, 0);
    __epsilon_closure(state_sets, nodes, num_nodes, next_sibling);

    state_count = 1;
    transitions = calloc(state_capacity * 256u, sizeof(uint32_t));
    if (!transitions) {
        free(scratch_set);
        free(state_sets);
        free(node_depths);
        free(nodes);
        return PROTECC_ERROR_OUT_OF_MEMORY;
    }

    while (queue_index < state_count) {
        const uint64_t* current = state_sets + (queue_index * words_per_state);

        for (unsigned int c = 0; c < 256u; c++) {
            memset(scratch_set, 0, words_per_state * sizeof(uint64_t));

            for (size_t n = 0; n < num_nodes; n++) {
                const protecc_node_t* node;

                if (!__bitset_test(current, n)) {
                    continue;
                }

                node = nodes[n];

                if ((node->type == NODE_WILDCARD_MULTI || node->type == NODE_WILDCARD_RECURSIVE) &&
                    __char_matches_node(node, (unsigned char)c, comp->flags)) {
                    __bitset_set(scratch_set, n);
                }

                if ((node->modifier == MODIFIER_ONE_OR_MORE || node->modifier == MODIFIER_ZERO_OR_MORE) &&
                    __char_matches_node(node, (unsigned char)c, comp->flags)) {
                    __bitset_set(scratch_set, n);
                }

                for (size_t k = 0; k < node->num_children; k++) {
                    const protecc_node_t* child = node->children[k];
                    size_t child_index;
                    size_t next_index = SIZE_MAX;

                    if (k + 1u < node->num_children) {
                        next_index = protecc_find_node_index(nodes, num_nodes, node->children[k + 1u]);
                    }

                    if (!__char_matches_node(child, (unsigned char)c, comp->flags)) {
                        continue;
                    }

                    child_index = protecc_find_node_index(nodes, num_nodes, child);
                    if (child_index == SIZE_MAX) {
                        free(accept);
                        free(transitions);
                        free(scratch_set);
                        free(state_sets);
                        free(node_depths);
                        free(nodes);
                        return PROTECC_ERROR_COMPILE_FAILED;
                    }
                    if (child->modifier == MODIFIER_NONE) {
                        __bitset_set(scratch_set, child_index);
                    } else if (child->modifier == MODIFIER_OPTIONAL) {
                        if (next_index != SIZE_MAX) {
                            __bitset_set(scratch_set, next_index);
                        } else {
                            __bitset_set(scratch_set, child_index);
                        }
                    } else if (child->modifier == MODIFIER_ONE_OR_MORE || child->modifier == MODIFIER_ZERO_OR_MORE) {
                        __bitset_set(scratch_set, child_index);
                    }
                }
            }

            __epsilon_closure(scratch_set, nodes, num_nodes, next_sibling);

            {
                ptrdiff_t existing = __dfa_find_state(state_sets, state_count, words_per_state, scratch_set);
                uint32_t target_state;

                if (existing >= 0) {
                    target_state = (uint32_t)existing;
                } else {
                    if ((uint32_t)state_count >= comp->config.max_states) {
                        free(accept);
                        free(transitions);
                        free(scratch_set);
                        free(state_sets);
                        free(next_sibling);
                        free(node_depths);
                        free(nodes);
                        return PROTECC_ERROR_COMPILE_FAILED;
                    }

                    if (__dfa_builder_state_add_state(&state, comp->config.max_states, words_per_state) != 0) {
                        free(accept);
                        free(transitions);
                        free(scratch_set);
                        free(state_sets);
                        free(next_sibling);
                        free(node_depths);
                        free(nodes);
                        return PROTECC_ERROR_COMPILE_FAILED;
                    }
                    target_state = (uint32_t)state_count;
                    state_count++;
                }

                transitions[(queue_index * 256u) + c] = target_state;
            }
        }

        queue_index++;
    }

    comp->dfa_accept_words = (uint32_t)((state_count + 31u) / 32u);
    accept = calloc(comp->dfa_accept_words, sizeof(uint32_t));
    perms = calloc(state_count, sizeof(uint32_t));
    if (!accept || !perms) {
        free(perms);
        free(accept);
        free(transitions);
        free(scratch_set);
        free(state_sets);
        free(node_depths);
        free(nodes);
        return PROTECC_ERROR_OUT_OF_MEMORY;
    }

    for (size_t i = 0; i < state_count; i++) {
        const uint64_t* state = state_sets + (i * words_per_state);
        if (__state_set_best_perms(state, nodes, node_depths, num_nodes, &perms[i])) {
            accept[i >> 5] |= (1u << (i & 31u));
        }
    }

    memset(comp->dfa_classmap, 0, sizeof(comp->dfa_classmap));
    for (unsigned int c = 0; c < 256u; c++) {
        comp->dfa_classmap[c] = (uint8_t)c;
    }

    free(comp->dfa_accept);
    free(comp->dfa_perms);
    free(comp->dfa_transitions);
    comp->dfa_accept = accept;
    comp->dfa_perms = perms;
    comp->dfa_transitions = transitions;
    comp->dfa_num_states = (uint32_t)state_count;
    comp->dfa_num_classes = 256u;
    comp->dfa_start_state = 0u;
    comp->has_dfa = true;

    free(scratch_set);
    free(state_sets);
    free(node_depths);
    free(next_sibling);
    free(nodes);
    return PROTECC_OK;
}
