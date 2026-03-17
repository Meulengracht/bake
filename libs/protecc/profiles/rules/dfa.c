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
    const protecc_node_t** node_keys;
    size_t*                node_values;
    size_t                 node_map_capacity;

    const protecc_node_t** nodes;
    size_t                 node_count;
    size_t*                next_sibling;

    size_t                 state_capacity;
    size_t                 state_count;
    uint64_t*              state_sets;
    uint64_t*              state_hashes;
    uint64_t               scratch_hash;
    uint64_t*              scratch_set;

    uint32_t*              transitions;
};

static size_t __bitset_words(size_t bitCount) {
    return (bitCount + 63u) / 64u;
}

static uint64_t __mix_u64(uint64_t value)
{
    value ^= value >> 30;
    value *= 0xbf58476d1ce4e5b9ULL;
    value ^= value >> 27;
    value *= 0x94d049bb133111ebULL;
    value ^= value >> 31;
    return value;
}

static uint64_t __ptr_hash(const void* ptr)
{
    return __mix_u64((uint64_t)(uintptr_t)ptr);
}

static uint64_t __bitset_hash(const uint64_t* bits, size_t words)
{
    uint64_t hash = 1469598103934665603ULL;

    for (size_t i = 0; i < words; i++) {
        hash ^= bits[i];
        hash *= 1099511628211ULL;
    }
    return hash;
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

    (void)flags;

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
    const uint64_t*              bitSet,
    const protecc_node_t* const* nodes,
    size_t                       nodeCount,
    uint32_t*                    out,
    size_t                       maxOut)
{
    bool   seen[PROTECC_MAX_RULES] = {false};
    size_t count = 0;

    for (size_t i = 0; i < nodeCount; i++) {
        uint32_t ruleIndex;

        if (!__bitset_test(bitSet, i) || !nodes[i]->is_terminal) {
            continue;
        }

        ruleIndex = (uint32_t)nodes[i]->perms;
        if (ruleIndex >= PROTECC_MAX_RULES) {
            continue;
        }

        if (seen[ruleIndex]) {
            continue;
        }

        seen[ruleIndex] = true;
        count++;
    }

    if (out != NULL && maxOut > 0) {
        size_t cursor = 0;

        for (uint32_t ruleIndex = 0; ruleIndex < PROTECC_MAX_RULES && cursor < maxOut; ruleIndex++) {
            if (seen[ruleIndex]) {
                out[cursor++] = ruleIndex;
            }
        }
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

static size_t __node_map_lookup(const struct __dfa_builder_state* state, const protecc_node_t* target)
{
    size_t   mask;
    uint64_t hash;
    size_t   slot;

    if (state->node_map_capacity == 0 || target == NULL) {
        return SIZE_MAX;
    }

    mask = state->node_map_capacity - 1u;
    hash = __ptr_hash(target);
    slot = (size_t)hash & mask;

    for (;;) {
        const protecc_node_t* key = state->node_keys[slot];

        if (key == NULL) {
            return SIZE_MAX;
        }

        if (key == target) {
            return state->node_values[slot];
        }

        slot = (slot + 1u) & mask;
    }
}

static int __node_map_insert(struct __dfa_builder_state* state, const protecc_node_t* key, size_t value)
{
    size_t   mask;
    uint64_t hash;
    size_t   slot;

    if (state->node_map_capacity == 0 || key == NULL) {
        return -1;
    }

    mask = state->node_map_capacity - 1u;
    hash = __ptr_hash(key);
    slot = (size_t)hash & mask;

    while (state->node_keys[slot] != NULL) {
        if (state->node_keys[slot] == key) {
            state->node_values[slot] = value;
            return 0;
        }
        slot = (slot + 1u) & mask;
    }

    state->node_keys[slot] = key;
    state->node_values[slot] = value;
    return 0;
}

static void __epsilon_closure(
    struct __dfa_builder_state* state,
    uint64_t*                bitSet)
{
    bool changed;

    do {
        changed = false;
        for (size_t i = 0; i < state->node_count; i++) {
            const protecc_node_t* node;

            if (!__bitset_test(bitSet, i)) {
                continue;
            }

            node = state->nodes[i];
            for (size_t c = 0; c < node->num_children; c++) {
                const protecc_node_t* child = node->children[c];
                size_t                child_index = __node_map_lookup(state, child);
                size_t                next_index = SIZE_MAX;

                if (child_index == SIZE_MAX) {
                    continue;
                }

                if (c + 1u < node->num_children) {
                    next_index = __node_map_lookup(state, node->children[c + 1u]);
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
    } while (changed);
}

static int __dfa_builder_setup(struct __dfa_builder_state* state, size_t wordsPerState)
{
    state->state_sets = calloc(state->state_capacity * wordsPerState, sizeof(uint64_t));
    state->state_hashes = calloc(state->state_capacity, sizeof(uint64_t));
    state->scratch_set = calloc(wordsPerState, sizeof(uint64_t));
    if (state->state_sets == NULL || state->state_hashes == NULL || state->scratch_set == NULL) {
        return -1;
    }

    __bitset_set(state->state_sets, 0);
    __epsilon_closure(state, state->state_sets);
    state->state_hashes[0] = __bitset_hash(state->state_sets, wordsPerState);

    state->state_count = 1;
    state->transitions = calloc(state->state_capacity * PROTECC_PROFILE_DFA_CLASSMAP_SIZE, sizeof(uint32_t));
    if (state->transitions == NULL) {
        return -1;
    }
    return 0;
}

static void __dfa_builder_cleanup(struct __dfa_builder_state* state)
{
    free(state->next_sibling);
    free(state->nodes);
    free(state->node_values);
    free(state->node_keys);
    free(state->state_sets);
    free(state->state_hashes);
    free(state->scratch_set);
    free(state->transitions);
}

static int __dfa_builder_resize_hashes(
    struct __dfa_builder_state* state,
    size_t                      newCapacity)
{
    uint64_t* newHashes;

    newHashes = realloc(state->state_hashes, newCapacity * sizeof(uint64_t));
    if (newHashes == NULL) {
        return PROTECC_ERROR_OUT_OF_MEMORY;
    }

    state->state_hashes = newHashes;
    memset(
        state->state_hashes + state->state_capacity,
        0,
        (newCapacity - state->state_capacity) * sizeof(uint64_t)
    );
    return 0;
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

        if (__dfa_builder_resize_hashes(state, newCapacity) != 0) {
            return PROTECC_ERROR_OUT_OF_MEMORY;
        }

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
    state->state_hashes[state->state_count] = state->scratch_hash;
    return 0;
}

static ptrdiff_t __dfa_builder_find_state(
    struct __dfa_builder_state* state,
    size_t                   wordsPerState)
{
    for (size_t i = 0; i < state->state_count; i++) {
        const uint64_t* state_set = state->state_sets + (i * wordsPerState);
        if (state->state_hashes[i] != state->scratch_hash) {
            continue;
        }
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
    state->node_map_capacity = 1u;
    while (state->node_map_capacity < (nodeCount * 2u)) {
        state->node_map_capacity <<= 1u;
    }

    state->node_keys = calloc(state->node_map_capacity, sizeof(*state->node_keys));
    state->node_values = calloc(state->node_map_capacity, sizeof(*state->node_values));

    if (state->nodes == NULL || state->next_sibling == NULL || state->node_keys == NULL || state->node_values == NULL) {
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
        if (__node_map_insert(state, state->nodes[i], i) != 0) {
            return -1;
        }
    }

    for (size_t i = 0; i < nodeCount; i++) {
        const protecc_node_t* node = state->nodes[i];
        for (size_t c = 0; c < node->num_children; c++) {
            size_t child_idx = __node_map_lookup(state, node->children[c]);
            if (child_idx == SIZE_MAX) {
                return -1;
            }

            if (c + 1u < node->num_children) {
                size_t next_idx = __node_map_lookup(state, node->children[c + 1u]);
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
    uint32_t                    acceptWordCount;
    protecc_rule_dfa_runtime_t* dfa;
    uint32_t*                   accept;
    uint32_t*                   candidate_index;
    uint32_t*                   candidate_count;
    uint32_t*                   candidates;
    uint32_t                    candidates_total = 0;
    uint32_t                    candidate_cursor = 0;
    uint32_t                    classes = PROTECC_PROFILE_DFA_CLASSMAP_SIZE;
    uint32_t                    tmp[PROTECC_MAX_RULES];

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

    if (accept == NULL || candidate_index == NULL || candidate_count == NULL || candidates == NULL || dfa == NULL) {
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
    struct __dfa_builder_state state = {
        .node_keys = NULL,
        .node_values = NULL,
        .node_map_capacity = 0,
        .nodes = NULL,
        .node_count = 0,
        .next_sibling = NULL,
        .state_capacity = 16,
        .state_count = 0,
        .state_sets = NULL,
        .state_hashes = NULL,
        .scratch_hash = 0,
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
                        nextIndex = __node_map_lookup(&state, node->children[k + 1u]);
                    }

                    if (!__char_matches_node(child, (unsigned char)c, local_profile.flags)) {
                        continue;
                    }

                    childIndex = __node_map_lookup(&state, child);
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
            state.scratch_hash = __bitset_hash(state.scratch_set, wordsPerState);
            if (__dfa_builder_add_transition(&state, queueIndex, c, wordsPerState, &local_profile)) {
                err = PROTECC_ERROR_COMPILE_FAILED;
                goto cleanup;
            }
        }

        queueIndex++;
    }

    err = __install_dfa_runtime(&state, wordsPerState, (uint32_t)pattern_count, outDfa);

cleanup:
    __dfa_builder_cleanup(&state);
    protecc_node_free(root);
    return err;
}

size_t __dfa_block_size(const protecc_rule_dfa_runtime_t* dfa)
{
    size_t base;

    if (dfa == NULL || !dfa->present) {
        return 0;
    }

    base = sizeof(protecc_profile_dfa_t);
    base += PROTECC_PROFILE_DFA_CLASSMAP_SIZE;
    base += (size_t)dfa->accept_words * sizeof(uint32_t);
    base += (size_t)dfa->num_states * sizeof(uint32_t); /* candidate_index */
    base += (size_t)dfa->num_states * sizeof(uint32_t); /* candidate_count */
    base += (size_t)dfa->candidates_total * sizeof(uint32_t);
    base += (size_t)dfa->num_states * (size_t)dfa->num_classes * sizeof(uint32_t);
    return base;
}

protecc_error_t __dfa_export_block(
    const protecc_rule_dfa_runtime_t* dfa,
    uint8_t*                         base,
    size_t                           bufferSize,
    uint32_t                         offset)
{
    protecc_profile_dfa_t header;
    uint8_t*          out;
    size_t            required;
    size_t            classmap_off;
    size_t            accept_off;
    size_t            cand_index_off;
    size_t            cand_count_off;
    size_t            candidates_off;
    size_t            transitions_off;

    if (dfa == NULL || !dfa->present) {
        return PROTECC_OK;
    }

    required = __dfa_block_size(dfa);
    if ((size_t)offset > bufferSize || required > bufferSize - offset) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    classmap_off = sizeof(protecc_profile_dfa_t);
    accept_off = classmap_off + PROTECC_PROFILE_DFA_CLASSMAP_SIZE;
    cand_index_off = accept_off + ((size_t)dfa->accept_words * sizeof(uint32_t));
    cand_count_off = cand_index_off + ((size_t)dfa->num_states * sizeof(uint32_t));
    candidates_off = cand_count_off + ((size_t)dfa->num_states * sizeof(uint32_t));
    transitions_off = candidates_off + ((size_t)dfa->candidates_total * sizeof(uint32_t));

    if (classmap_off > UINT32_MAX || accept_off > UINT32_MAX || cand_index_off > UINT32_MAX
        || cand_count_off > UINT32_MAX || candidates_off > UINT32_MAX || transitions_off > UINT32_MAX) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    memset(&header, 0, sizeof(header));
    header.num_states = dfa->num_states;
    header.num_classes = dfa->num_classes;
    header.start_state = dfa->start_state;
    header.accept_words = dfa->accept_words;
    header.classmap_off = (uint32_t)classmap_off;
    header.accept_off = (uint32_t)accept_off;
    header.candidate_index_off = (uint32_t)cand_index_off;
    header.candidate_count_off = (uint32_t)cand_count_off;
    header.candidates_off = (uint32_t)candidates_off;
    header.candidates_count = dfa->candidates_total;
    header.transitions_off = (uint32_t)transitions_off;

    out = base + offset;
    memcpy(out, &header, sizeof(header));
    memcpy(out + classmap_off, dfa->classmap, PROTECC_PROFILE_DFA_CLASSMAP_SIZE);
    memcpy(out + accept_off, dfa->accept, (size_t)dfa->accept_words * sizeof(uint32_t));
    memcpy(out + cand_index_off, dfa->candidate_index, (size_t)dfa->num_states * sizeof(uint32_t));
    memcpy(out + cand_count_off, dfa->candidate_count, (size_t)dfa->num_states * sizeof(uint32_t));
    memcpy(out + candidates_off, dfa->candidates, (size_t)dfa->candidates_total * sizeof(uint32_t));
    memcpy(
        out + transitions_off,
        dfa->transitions,
        (size_t)dfa->num_states * (size_t)dfa->num_classes * sizeof(uint32_t)
    );
    return PROTECC_OK;
}

static protecc_error_t __dfa_validate_candidate_table(
    const uint8_t* blockBase,
    size_t         blockOffset,
    size_t         candidateCountOffset,
    size_t         candidateIndexOffset,
    uint32_t       numStates,
    uint32_t       candidatesCount,
    size_t         ruleCount)
{
    const uint32_t* counts = (const uint32_t*)(blockBase + blockOffset + candidateCountOffset);
    const uint32_t* starts = (const uint32_t*)(blockBase + blockOffset + candidateIndexOffset);
    size_t          total = 0;

    for (uint32_t i = 0; i < numStates; i++) {
        uint32_t count = counts[i];
        uint32_t start = starts[i];

        if (count > ruleCount) {
            return PROTECC_ERROR_INVALID_BLOB;
        }

        if ((uint64_t)start + (uint64_t)count > candidatesCount) {
            return PROTECC_ERROR_INVALID_BLOB;
        }

        total += count;
    }

    if (total != candidatesCount) {
        return PROTECC_ERROR_INVALID_BLOB;
    }
    return PROTECC_OK;
}

static protecc_error_t __dfa_validate_transition_table(
    const uint8_t* blockBase,
    size_t         blockOffset,
    size_t         transitionsOffset,
    size_t         transitionsCount,
    uint32_t       numStates)
{
    const uint32_t* transitions = (const uint32_t*)(blockBase + blockOffset + transitionsOffset);

    for (size_t i = 0; i < transitionsCount; i++) {
        if (transitions[i] >= numStates) {
            return PROTECC_ERROR_INVALID_BLOB;
        }
    }
    return PROTECC_OK;
}

protecc_error_t __dfa_validate_block(
    const uint8_t* blockBase,
    size_t         bufferSize,
    size_t         blockOffset,
    size_t         ruleCount,
    size_t*        blockSizeOut)
{
    protecc_profile_dfa_t header;
    size_t            classmap_off;
    size_t            accept_off;
    size_t            cand_index_off;
    size_t            cand_count_off;
    size_t            candidates_off;
    size_t            transitions_off;
    size_t            required;
    size_t            transitions_count;
    uint64_t          tmp;

    if (blockOffset > bufferSize || bufferSize - blockOffset < sizeof(protecc_profile_dfa_t)) {
        return PROTECC_ERROR_INVALID_BLOB;
    }

    memcpy(&header, blockBase + blockOffset, sizeof(header));

    if (header.num_states == 0 || header.num_classes == 0 || header.num_classes > PROTECC_PROFILE_DFA_CLASSMAP_SIZE) {
        return PROTECC_ERROR_INVALID_BLOB;
    }

    if (header.accept_words != ((header.num_states + 31u) / 32u)) {
        return PROTECC_ERROR_INVALID_BLOB;
    }

    classmap_off = header.classmap_off;
    accept_off = header.accept_off;
    cand_index_off = header.candidate_index_off;
    cand_count_off = header.candidate_count_off;
    candidates_off = header.candidates_off;
    transitions_off = header.transitions_off;

    required = sizeof(protecc_profile_dfa_t)
        + PROTECC_PROFILE_DFA_CLASSMAP_SIZE
        + ((size_t)header.accept_words * sizeof(uint32_t))
        + ((size_t)header.num_states * sizeof(uint32_t))
        + ((size_t)header.num_states * sizeof(uint32_t))
        + ((size_t)header.candidates_count * sizeof(uint32_t));

    tmp = (uint64_t)header.num_states * (uint64_t)header.num_classes;
    if (tmp > SIZE_MAX / sizeof(uint32_t)) {
        return PROTECC_ERROR_INVALID_BLOB;
    }
    transitions_count = (size_t)tmp;
    if (transitions_off < candidates_off + ((size_t)header.candidates_count * sizeof(uint32_t))) {
        return PROTECC_ERROR_INVALID_BLOB;
    }

    required = transitions_off + (transitions_count * sizeof(uint32_t));

    if (required > bufferSize - blockOffset) {
        return PROTECC_ERROR_INVALID_BLOB;
    }

    if (classmap_off < sizeof(protecc_profile_dfa_t)) {
        return PROTECC_ERROR_INVALID_BLOB;
    }

    if (accept_off < classmap_off + PROTECC_PROFILE_DFA_CLASSMAP_SIZE
        || cand_index_off < accept_off + ((size_t)header.accept_words * sizeof(uint32_t))
        || cand_count_off < cand_index_off + ((size_t)header.num_states * sizeof(uint32_t))
        || candidates_off < cand_count_off + ((size_t)header.num_states * sizeof(uint32_t))
        || transitions_off < candidates_off + ((size_t)header.candidates_count * sizeof(uint32_t))) {
        return PROTECC_ERROR_INVALID_BLOB;
    }

    if (__dfa_validate_candidate_table(
            blockBase,
            blockOffset,
            cand_count_off,
            cand_index_off,
            header.num_states,
            header.candidates_count,
            ruleCount) != PROTECC_OK) {
        return PROTECC_ERROR_INVALID_BLOB;
    }

    if (__dfa_validate_transition_table(
            blockBase,
            blockOffset,
            transitions_off,
            transitions_count,
            header.num_states) != PROTECC_OK) {
        return PROTECC_ERROR_INVALID_BLOB;
    }

    if (blockSizeOut) {
        *blockSizeOut = required;
    }

    return PROTECC_OK;
}

void __dfa_free_runtime(protecc_rule_dfa_runtime_t* dfa)
{
    if (dfa == NULL) {
        return;
    }

    free(dfa->accept);
    free(dfa->candidate_index);
    free(dfa->candidate_count);
    free(dfa->candidates);
    free(dfa->transitions);
    free(dfa);
}
