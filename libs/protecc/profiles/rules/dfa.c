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

struct __dfa_builder_state {
    const protecc_node_t** nodes;
    size_t                 node_count;
    size_t*                next_sibling;

    size_t                 state_capacity;
    size_t                 state_count;
    uint64_t*              state_sets;
    uint64_t*              scratch_set;

    uint32_t*              transitions;
};

static size_t __bitset_words(size_t bitCount) {
    return (bitCount + 63u) / 64u;
}

static void __bitset_set(uint64_t* bits, size_t index) {
    bits[index >> 6] |= (1ull << (index & 63u));
}

static bool __bitset_test(const uint64_t* bits, size_t index) {
    return (bits[index >> 6] & (1ull << (index & 63u))) != 0;
}

static bool __char_matches_node(const protecc_node_t* node, unsigned char c, uint32_t flags)
{
    char ch = (char)c;

    if (flags & PROTECC_FLAG_CASE_INSENSITIVE) {
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

static size_t __collect_state_candidates(
    const uint64_t*               bitSet,
    const protecc_node_t* const*  nodes,
    size_t                        nodeCount,
    uint32_t*                     out,
    size_t                        maxOut)
{
    size_t count = 0;

    for (size_t i = 0; i < nodeCount; i++) {
        uint32_t rule_index;

        if (!__bitset_test(bitSet, i) || !nodes[i]->is_terminal) {
            continue;
        }

        rule_index = (uint32_t)nodes[i]->perms;
        if (rule_index >= PROTECC_MAX_RULES) {
            continue;
        }

        bool already_present = false;
        for (size_t j = 0; j < count; j++) {
            if (out[j] == rule_index) {
                already_present = true;
                break;
            }
        }
        if (already_present) {
            continue;
        }

        if (count < maxOut) {
            size_t insert_pos = count;
            for (size_t j = 0; j < count; j++) {
                if (rule_index < out[j]) {
                    insert_pos = j;
                    break;
                }
            }

            if (insert_pos < count) {
                memmove(&out[insert_pos + 1u], &out[insert_pos], (count - insert_pos) * sizeof(uint32_t));
                out[insert_pos] = rule_index;
            } else {
                out[count] = rule_index;
            }
        }
        count++;
    }

    return count;
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

static void __epsilon_closure(
    struct __dfa_builder_state* state,
    uint64_t*                bitSet)
{
    bool changed = true;

    while (changed) {
        changed = false;
        for (size_t i = 0; i < state->node_count; i++) {
            const protecc_node_t* node;

            if (!__bitset_test(bitSet, i)) {
                continue;
            }

            node = state->nodes[i];
            for (size_t c = 0; c < node->num_children; c++) {
                const protecc_node_t* child = node->children[c];
                size_t                child_index = __find_node_index(state->nodes, state->node_count, child);
                size_t                next_index = SIZE_MAX;

                if (child_index == SIZE_MAX) {
                    continue;
                }

                if (c + 1u < node->num_children) {
                    next_index = __find_node_index(state->nodes, state->node_count, node->children[c + 1u]);
                }

                if (child->modifier == MODIFIER_OPTIONAL || child->modifier == MODIFIER_ZERO_OR_MORE) {
                    if (next_index != SIZE_MAX && !__bitset_test(bitSet, next_index)) {
                        __bitset_set(bitSet, next_index);
                        changed = true;
                    } else if (next_index == SIZE_MAX && !__bitset_test(bitSet, child_index)) {
                        __bitset_set(bitSet, child_index);
                        changed = true;
                    }
                }

                if (child->type == NODE_WILDCARD_MULTI || child->type == NODE_WILDCARD_RECURSIVE) {
                    if (!__bitset_test(bitSet, child_index)) {
                        __bitset_set(bitSet, child_index);
                        changed = true;
                    }
                }
            }

            if (node->modifier == MODIFIER_ONE_OR_MORE || node->modifier == MODIFIER_ZERO_OR_MORE ||
                node->modifier == MODIFIER_OPTIONAL) {
                size_t next_index = state->next_sibling[i];
                if (next_index != SIZE_MAX && !__bitset_test(bitSet, next_index)) {
                    __bitset_set(bitSet, next_index);
                    changed = true;
                }
            }
        }
    }
}

static int __dfa_builder_setup(struct __dfa_builder_state* state, size_t wordsPerState)
{
    state->state_sets = calloc(state->state_capacity * wordsPerState, sizeof(uint64_t));
    state->scratch_set = calloc(wordsPerState, sizeof(uint64_t));
    if (!state->state_sets || !state->scratch_set) {
        return -1;
    }

    __bitset_set(state->state_sets, 0);
    __epsilon_closure(state, state->state_sets);

    state->state_count = 1;
    state->transitions = calloc(state->state_capacity * PROTECC_PROFILE_DFA_CLASSMAP_SIZE, sizeof(uint32_t));
    if (!state->transitions) {
        return -1;
    }
    return 0;
}

static void __dfa_builder_cleanup(struct __dfa_builder_state* state, protecc_error_t err)
{
    free(state->next_sibling);
    free(state->nodes);

    if (err != PROTECC_OK) {
        free(state->state_sets);
        free(state->scratch_set);
        free(state->transitions);
    } else {
        free(state->state_sets);
        free(state->scratch_set);
    }
}

static int __dfa_builder_add_state(
    struct __dfa_builder_state* state,
    size_t                   maxStateCount,
    size_t                   wordsPerState)
{
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

        newTransitions = realloc(state->transitions, newCapacity * PROTECC_PROFILE_DFA_CLASSMAP_SIZE * sizeof(uint32_t));
        if (!newTransitions) {
            return PROTECC_ERROR_OUT_OF_MEMORY;
        }

        state->transitions = newTransitions;
        memset(
            state->transitions + (state->state_capacity * PROTECC_PROFILE_DFA_CLASSMAP_SIZE),
            0,
            (newCapacity - state->state_capacity) * PROTECC_PROFILE_DFA_CLASSMAP_SIZE * sizeof(uint32_t)
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

static ptrdiff_t __dfa_builder_find_state(
    struct __dfa_builder_state* state,
    size_t                   wordsPerState)
{
    for (size_t i = 0; i < state->state_count; i++) {
        const uint64_t* state_set = state->state_sets + (i * wordsPerState);
        if (memcmp(state_set, state->scratch_set, wordsPerState * sizeof(uint64_t)) == 0) {
            return (ptrdiff_t)i;
        }
    }
    return -1;
}

static int __dfa_builder_add_transition(
    struct __dfa_builder_state* state,
    size_t                   queueIndex,
    uint32_t                 c,
    size_t                   wordsPerState,
    const protecc_profile_t* profile)
{
    ptrdiff_t existing = __dfa_builder_find_state(state, wordsPerState);
    uint32_t  nextState;
    int       status;

    if (existing >= 0) {
        nextState = (uint32_t)existing;
    } else {
        if ((uint32_t)state->state_count >= profile->config.max_states) {
            return -1;
        }

        status = __dfa_builder_add_state(state, profile->config.max_states, wordsPerState);
        if (status != 0) {
            return status;
        }
        nextState = (uint32_t)state->state_count;
        state->state_count++;
    }

    state->transitions[(queueIndex * PROTECC_PROFILE_DFA_CLASSMAP_SIZE) + c] = nextState;
    return 0;
}

static int __dfa_builder_initialize_nodes(
    struct __dfa_builder_state* state,
    const protecc_profile_t* profile,
    size_t                   nodeCount)
{
    size_t index = 0;

    state->nodes = calloc(nodeCount, sizeof(*state->nodes));
    state->next_sibling = malloc(nodeCount * sizeof(*state->next_sibling));
    if (!state->nodes || !state->next_sibling) {
        return -1;
    }
    state->node_count = nodeCount;

    for (size_t i = 0; i < nodeCount; i++) {
        state->next_sibling[i] = SIZE_MAX;
    }

    __collect_nodes(profile->root, state->nodes, &index);
    if (index != nodeCount) {
        return -1;
    }

    for (size_t i = 0; i < nodeCount; i++) {
        const protecc_node_t* node = state->nodes[i];
        for (size_t c = 0; c < node->num_children; c++) {
            size_t child_idx = __find_node_index(state->nodes, nodeCount, node->children[c]);
            if (child_idx == SIZE_MAX) {
                return -1;
            }

            if (c + 1u < node->num_children) {
                size_t next_idx = __find_node_index(state->nodes, nodeCount, node->children[c + 1u]);
                if (next_idx == SIZE_MAX) {
                    return -1;
                }
                state->next_sibling[child_idx] = next_idx;
            }
        }
    }
    return 0;
}

static protecc_error_t __install_dfa_runtime(
    struct __dfa_builder_state*  state,
    size_t                    wordsPerState,
    uint32_t                  rule_count,
    protecc_rule_dfa_runtime_t** outDfa)
{
    uint32_t                 acceptWordCount;
    protecc_rule_dfa_runtime_t* dfa;
    uint32_t*                accept;
    uint32_t*                candidate_index;
    uint32_t*                candidate_count;
    uint32_t*                candidates;
    uint32_t                 candidates_total = 0;
    uint32_t                 candidate_cursor = 0;
    uint32_t                 classes = PROTECC_PROFILE_DFA_CLASSMAP_SIZE;
    uint32_t                 tmp[PROTECC_MAX_RULES];

    acceptWordCount = (uint32_t)((state->state_count + 31u) / 32u);

    for (size_t i = 0; i < state->state_count; i++) {
        const uint64_t* set = state->state_sets + (i * wordsPerState);
        size_t count = __collect_state_candidates(set, state->nodes, state->node_count, tmp, PROTECC_MAX_RULES);

        if (count > PROTECC_MAX_RULES) {
            return PROTECC_ERROR_COMPILE_FAILED;
        }

        if (candidates_total > UINT32_MAX - (uint32_t)count) {
            return PROTECC_ERROR_COMPILE_FAILED;
        }
        candidates_total += (uint32_t)count;
    }

    accept = calloc(acceptWordCount, sizeof(uint32_t));
    candidate_index = calloc(state->state_count, sizeof(uint32_t));
    candidate_count = calloc(state->state_count, sizeof(uint32_t));
    candidates = calloc(candidates_total, sizeof(uint32_t));
    dfa = calloc(1, sizeof(protecc_rule_dfa_runtime_t));

    if (!accept || !candidate_index || !candidate_count || !candidates || !dfa) {
        free(dfa);
        free(candidates);
        free(candidate_count);
        free(candidate_index);
        free(accept);
        return PROTECC_ERROR_OUT_OF_MEMORY;
    }

    for (size_t i = 0; i < state->state_count; i++) {
        const uint64_t* set = state->state_sets + (i * wordsPerState);
        size_t count = __collect_state_candidates(set, state->nodes, state->node_count, tmp, PROTECC_MAX_RULES);

        candidate_index[i] = candidate_cursor;
        candidate_count[i] = (uint32_t)count;

        if (count > 0) {
            accept[i >> 5] |= (1u << (i & 31u));
            for (size_t c = 0; c < count; c++) {
                candidates[candidate_cursor++] = tmp[c];
            }
        }
    }

    memset(dfa->classmap, 0, sizeof(dfa->classmap));
    for (unsigned int c = 0; c < PROTECC_PROFILE_DFA_CLASSMAP_SIZE; c++) {
        dfa->classmap[c] = (uint8_t)c;
    }

    dfa->accept_words = acceptWordCount;
    dfa->accept = accept;
    dfa->candidate_index = candidate_index;
    dfa->candidate_count = candidate_count;
    dfa->candidates = candidates;
    dfa->candidates_total = candidates_total;
    dfa->transitions = state->transitions;
    dfa->num_states = (uint32_t)state->state_count;
    dfa->num_classes = classes;
    dfa->start_state = 0u;
    dfa->present = true;
    dfa->rule_count = rule_count;

    state->transitions = NULL;

    *outDfa = dfa;
    return PROTECC_OK;
}

protecc_error_t __build_dfa_from_patterns(
    const protecc_rule_dfa_pattern_t*  patterns,
    size_t                             pattern_count,
    const protecc_profile_t*           source_profile,
    protecc_rule_dfa_runtime_t**       outDfa)
{
    protecc_error_t          err = PROTECC_OK;
    protecc_profile_t        local_profile = {0};
    protecc_node_t*          root = NULL;
    struct __dfa_builder_state  state = {
        .nodes = NULL,
        .node_count = 0,
        .next_sibling = NULL,
        .state_capacity = 16,
        .state_count = 0,
        .state_sets = NULL,
        .scratch_set = NULL,
        .transitions = NULL
    };
    size_t wordsPerState;
    size_t queueIndex = 0;
    size_t maxDepth = 0;
    size_t edgeCount = 0;

    if (pattern_count == 0) {
        *outDfa = NULL;
        return PROTECC_OK;
    }

    root = protecc_node_new(NODE_LITERAL);
    if (root == NULL) {
        return PROTECC_ERROR_OUT_OF_MEMORY;
    }

    local_profile.flags = source_profile->flags;
    local_profile.config = source_profile->config;
    local_profile.root = root;

    for (size_t i = 0; i < pattern_count; i++) {
        protecc_node_t* terminal = NULL;

        err = protecc_parse_pattern(patterns[i].pattern, root, local_profile.flags, &terminal);
        if (err != PROTECC_OK) {
            goto cleanup;
        }

        if (terminal != NULL) {
            terminal->perms = patterns[i].rule_index;
        }
    }

    protecc_node_collect_stats(local_profile.root, 0, &state.node_count, &maxDepth, &edgeCount);
    (void)maxDepth;
    (void)edgeCount;
    if (state.node_count == 0) {
        err = PROTECC_ERROR_COMPILE_FAILED;
        goto cleanup;
    }

    if (__dfa_builder_initialize_nodes(&state, &local_profile, state.node_count)) {
        err = PROTECC_ERROR_COMPILE_FAILED;
        goto cleanup;
    }

    wordsPerState = __bitset_words(state.node_count);
    if (__dfa_builder_setup(&state, wordsPerState)) {
        err = PROTECC_ERROR_OUT_OF_MEMORY;
        goto cleanup;
    }

    while (queueIndex < state.state_count) {
        const uint64_t* current = state.state_sets + (queueIndex * wordsPerState);

        for (uint32_t c = 0; c < PROTECC_PROFILE_DFA_CLASSMAP_SIZE; c++) {
            memset(state.scratch_set, 0, wordsPerState * sizeof(uint64_t));

            for (size_t n = 0; n < state.node_count; n++) {
                const protecc_node_t* node;

                if (!__bitset_test(current, n)) {
                    continue;
                }

                node = state.nodes[n];

                if ((node->type == NODE_WILDCARD_MULTI || node->type == NODE_WILDCARD_RECURSIVE) &&
                    __char_matches_node(node, (unsigned char)c, local_profile.flags)) {
                    __bitset_set(state.scratch_set, n);
                }

                if ((node->modifier == MODIFIER_ONE_OR_MORE || node->modifier == MODIFIER_ZERO_OR_MORE) &&
                    __char_matches_node(node, (unsigned char)c, local_profile.flags)) {
                    __bitset_set(state.scratch_set, n);
                }

                for (size_t k = 0; k < node->num_children; k++) {
                    const protecc_node_t* child = node->children[k];
                    size_t                nextIndex = SIZE_MAX;
                    size_t                childIndex;

                    if (k + 1u < node->num_children) {
                        nextIndex = __find_node_index(state.nodes, state.node_count, node->children[k + 1u]);
                    }

                    if (!__char_matches_node(child, (unsigned char)c, local_profile.flags)) {
                        continue;
                    }

                    childIndex = __find_node_index(state.nodes, state.node_count, child);
                    if (childIndex == SIZE_MAX) {
                        err = PROTECC_ERROR_COMPILE_FAILED;
                        goto cleanup;
                    }

                    if (child->modifier == MODIFIER_NONE) {
                        __bitset_set(state.scratch_set, childIndex);
                    } else if (child->modifier == MODIFIER_OPTIONAL) {
                        if (nextIndex != SIZE_MAX) {
                            __bitset_set(state.scratch_set, nextIndex);
                        } else {
                            __bitset_set(state.scratch_set, childIndex);
                        }
                    } else if (child->modifier == MODIFIER_ONE_OR_MORE || child->modifier == MODIFIER_ZERO_OR_MORE) {
                        __bitset_set(state.scratch_set, childIndex);
                    }
                }
            }

            __epsilon_closure(&state, state.scratch_set);
            if (__dfa_builder_add_transition(&state, queueIndex, c, wordsPerState, &local_profile)) {
                err = PROTECC_ERROR_COMPILE_FAILED;
                goto cleanup;
            }
        }

        queueIndex++;
    }

    err = __install_dfa_runtime(&state, wordsPerState, (uint32_t)pattern_count, outDfa);

cleanup:
    __dfa_builder_cleanup(&state, err);
    protecc_node_free(root);
    return err;
}
