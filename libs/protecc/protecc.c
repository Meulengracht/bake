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

#include "private.h"

struct protecc_profile_builder {
    protecc_pattern_t*   patterns;
    size_t               pattern_count;
    size_t               pattern_capacity;

    protecc_net_rule_t*  net_rules;
    size_t               net_rule_count;
    size_t               net_rule_capacity;

    protecc_mount_rule_t* mount_rules;
    size_t                mount_rule_count;
    size_t                mount_rule_capacity;
};

static protecc_error_t __build_trie_patterns(
    protecc_compiled_t*             comp,
    const protecc_pattern_t*        patterns,
    size_t                          count,
    uint32_t                        flags);

static protecc_error_t __finalize_compilation(protecc_compiled_t* comp);

static bool __is_valid_net_protocol(protecc_net_protocol_t protocol)
{
    return protocol == PROTECC_NET_PROTOCOL_ANY
        || protocol == PROTECC_NET_PROTOCOL_TCP
        || protocol == PROTECC_NET_PROTOCOL_UDP
        || protocol == PROTECC_NET_PROTOCOL_UNIX;
}

static bool __is_valid_net_family(protecc_net_family_t family)
{
    return family == PROTECC_NET_FAMILY_ANY
        || family == PROTECC_NET_FAMILY_IPV4
        || family == PROTECC_NET_FAMILY_IPV6
        || family == PROTECC_NET_FAMILY_UNIX;
}

static bool __is_valid_action(protecc_action_t action)
{
    return action == PROTECC_ACTION_ALLOW
        || action == PROTECC_ACTION_DENY
        || action == PROTECC_ACTION_AUDIT;
}

static protecc_error_t __validate_net_rule(const protecc_net_rule_t* rule)
{
    if (!rule) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    if (!__is_valid_action(rule->action)
        || !__is_valid_net_protocol(rule->protocol)
        || !__is_valid_net_family(rule->family)
        || rule->port_from > rule->port_to) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    if (rule->protocol == PROTECC_NET_PROTOCOL_UNIX) {
        if (rule->family == PROTECC_NET_FAMILY_IPV4 || rule->family == PROTECC_NET_FAMILY_IPV6) {
            return PROTECC_ERROR_INVALID_ARGUMENT;
        }
        if (rule->port_from != 0 || rule->port_to != 0) {
            return PROTECC_ERROR_INVALID_ARGUMENT;
        }
    }

    if (rule->family == PROTECC_NET_FAMILY_UNIX) {
        if (rule->protocol == PROTECC_NET_PROTOCOL_TCP || rule->protocol == PROTECC_NET_PROTOCOL_UDP) {
            return PROTECC_ERROR_INVALID_ARGUMENT;
        }
    }

    if (rule->unix_path_pattern) {
        protecc_error_t path_err = protecc_validate_pattern(rule->unix_path_pattern);
        if (path_err != PROTECC_OK) {
            return path_err;
        }
    }

    return PROTECC_OK;
}

static protecc_error_t __validate_mount_rule(const protecc_mount_rule_t* rule)
{
    if (!rule) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    if (!__is_valid_action(rule->action)) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    if (rule->source_pattern) {
        protecc_error_t source_err = protecc_validate_pattern(rule->source_pattern);
        if (source_err != PROTECC_OK) {
            return source_err;
        }
    }

    if (rule->target_pattern) {
        protecc_error_t target_err = protecc_validate_pattern(rule->target_pattern);
        if (target_err != PROTECC_OK) {
            return target_err;
        }
    }

    return PROTECC_OK;
}

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

static size_t protecc_profile_size(uint32_t num_nodes, uint32_t num_edges) {
    return sizeof(protecc_profile_header_t)
        + (size_t)num_nodes * sizeof(protecc_profile_node_t)
        + (size_t)num_edges * sizeof(uint32_t);
}

static size_t protecc_profile_dfa_size(uint32_t num_states, uint32_t num_classes, uint32_t accept_words) {
    return sizeof(protecc_profile_header_t)
        + sizeof(protecc_profile_dfa_t)
        + PROTECC_PROFILE_DFA_CLASSMAP_SIZE
        + ((size_t)accept_words * sizeof(uint32_t))
        + ((size_t)num_states * sizeof(uint32_t))
        + ((size_t)num_states * (size_t)num_classes * sizeof(uint32_t));
}

static bool __match_dfa(
    const protecc_compiled_t* compiled,
    const char* path,
    size_t path_len,
    protecc_permission_t* perms_out
) {
    uint32_t state;

    if (!compiled || !compiled->has_dfa || !compiled->dfa_transitions || !compiled->dfa_accept ||
        !compiled->dfa_perms || !path || !perms_out) {
        return false;
    }

    *perms_out = PROTECC_PERM_NONE;

    state = compiled->dfa_start_state;
    for (size_t i = 0; i < path_len; i++) {
        uint8_t c = (uint8_t)path[i];
        uint32_t cls = compiled->dfa_classmap[c];
        uint64_t index;

        if (cls >= compiled->dfa_num_classes) {
            return false;
        }

        index = ((uint64_t)state * (uint64_t)compiled->dfa_num_classes) + (uint64_t)cls;
        state = compiled->dfa_transitions[index];
        if (state >= compiled->dfa_num_states) {
            return false;
        }
    }

    if ((compiled->dfa_accept[state >> 5] & (1u << (state & 31u))) == 0u) {
        return false;
    }

    *perms_out = (protecc_permission_t)compiled->dfa_perms[state];
    return true;
}

void protecc_compile_config_default(protecc_compile_config_t* config) {
    if (!config) {
        return;
    }

    config->mode = PROTECC_COMPILE_MODE_TRIE;
    config->max_patterns = 256;
    config->max_pattern_length = 128;
    config->max_states = 2048;
    config->max_classes = 32;
}

static protecc_error_t protecc_update_stats(protecc_compiled_t* compiled)
{
    size_t num_nodes = 0;
    size_t max_depth = 0;
    size_t num_edges = 0;

    if (!compiled || !compiled->root) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }
    protecc_node_collect_stats(compiled->root, 0, &num_nodes, &max_depth, &num_edges);

    if (num_nodes > UINT32_MAX || num_edges > UINT32_MAX) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    compiled->stats.num_nodes = num_nodes;
    compiled->stats.max_depth = max_depth;
    compiled->stats.binary_size = protecc_profile_size((uint32_t)num_nodes, (uint32_t)num_edges);

    return PROTECC_OK;
}

const char* protecc_error_string(protecc_error_t error) {
    switch (error) {
        case PROTECC_OK:
            return "Success";
        case PROTECC_ERROR_INVALID_PATTERN:
            return "Invalid pattern";
        case PROTECC_ERROR_OUT_OF_MEMORY:
            return "Out of memory";
        case PROTECC_ERROR_INVALID_ARGUMENT:
            return "Invalid argument";
        case PROTECC_ERROR_COMPILE_FAILED:
            return "Compilation failed";
        case PROTECC_ERROR_NOT_SUPPORTED:
            return "Feature not supported";
        default:
            return "Unknown error";
    }
}

static protecc_error_t __builder_reserve(
    void**  storage,
    size_t* capacity,
    size_t  count,
    size_t  element_size)
{
    size_t new_capacity;
    void* resized;

    if (!storage || !capacity || element_size == 0) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    if (count <= *capacity) {
        return PROTECC_OK;
    }

    new_capacity = (*capacity == 0) ? 8 : *capacity;
    while (new_capacity < count) {
        if (new_capacity > (SIZE_MAX / 2)) {
            return PROTECC_ERROR_OUT_OF_MEMORY;
        }
        new_capacity *= 2;
    }

    if (new_capacity > (SIZE_MAX / element_size)) {
        return PROTECC_ERROR_OUT_OF_MEMORY;
    }

    resized = realloc(*storage, new_capacity * element_size);
    if (!resized) {
        return PROTECC_ERROR_OUT_OF_MEMORY;
    }

    *storage = resized;
    *capacity = new_capacity;
    return PROTECC_OK;
}

static char* __dup_string(const char* value)
{
    size_t length;
    char* copy;

    if (!value) {
        return NULL;
    }

    length = strlen(value) + 1;
    copy = malloc(length);
    if (!copy) {
        return NULL;
    }
    memcpy(copy, value, length);
    return copy;
}

static void __free_builder_patterns(protecc_profile_builder_t* builder)
{
    if (!builder || !builder->patterns) {
        return;
    }

    for (size_t i = 0; i < builder->pattern_count; i++) {
        free((void*)builder->patterns[i].pattern);
    }

    free(builder->patterns);
    builder->patterns = NULL;
    builder->pattern_count = 0;
    builder->pattern_capacity = 0;
}

static void __free_builder_net_rules(protecc_profile_builder_t* builder)
{
    if (!builder || !builder->net_rules) {
        return;
    }

    for (size_t i = 0; i < builder->net_rule_count; i++) {
        free((void*)builder->net_rules[i].ip_pattern);
        free((void*)builder->net_rules[i].unix_path_pattern);
    }

    free(builder->net_rules);
    builder->net_rules = NULL;
    builder->net_rule_count = 0;
    builder->net_rule_capacity = 0;
}

static void __free_builder_mount_rules(protecc_profile_builder_t* builder)
{
    if (!builder || !builder->mount_rules) {
        return;
    }

    for (size_t i = 0; i < builder->mount_rule_count; i++) {
        free((void*)builder->mount_rules[i].source_pattern);
        free((void*)builder->mount_rules[i].target_pattern);
        free((void*)builder->mount_rules[i].fstype_pattern);
        free((void*)builder->mount_rules[i].options_pattern);
    }

    free(builder->mount_rules);
    builder->mount_rules = NULL;
    builder->mount_rule_count = 0;
    builder->mount_rule_capacity = 0;
}

protecc_profile_builder_t* protecc_profile_builder_create(void)
{
    return calloc(1, sizeof(protecc_profile_builder_t));
}

void protecc_profile_builder_reset(protecc_profile_builder_t* builder)
{
    if (!builder) {
        return;
    }

    __free_builder_patterns(builder);
    __free_builder_net_rules(builder);
    __free_builder_mount_rules(builder);
}

void protecc_profile_builder_destroy(protecc_profile_builder_t* builder)
{
    if (!builder) {
        return;
    }

    protecc_profile_builder_reset(builder);
    free(builder);
}

protecc_error_t protecc_profile_builder_add_patterns(
    protecc_profile_builder_t* builder,
    const protecc_pattern_t*   patterns,
    size_t                     count)
{
    protecc_error_t reserve_err;

    if (!builder || !patterns || count == 0) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    reserve_err = __builder_reserve((void**)&builder->patterns,
                                    &builder->pattern_capacity,
                                    builder->pattern_count + count,
                                    sizeof(protecc_pattern_t));
    if (reserve_err != PROTECC_OK) {
        return reserve_err;
    }

    for (size_t i = 0; i < count; i++) {
        char* pattern_copy;
        protecc_error_t validation_err;

        if (!patterns[i].pattern) {
            return PROTECC_ERROR_INVALID_ARGUMENT;
        }

        validation_err = protecc_validate_pattern(patterns[i].pattern);
        if (validation_err != PROTECC_OK) {
            return validation_err;
        }

        pattern_copy = __dup_string(patterns[i].pattern);
        if (!pattern_copy) {
            return PROTECC_ERROR_OUT_OF_MEMORY;
        }

        builder->patterns[builder->pattern_count].pattern = pattern_copy;
        builder->patterns[builder->pattern_count].perms = patterns[i].perms;
        builder->pattern_count++;
    }

    return PROTECC_OK;
}

protecc_error_t protecc_profile_builder_add_net_rule(
    protecc_profile_builder_t* builder,
    const protecc_net_rule_t*  rule)
{
    protecc_error_t reserve_err;
    protecc_error_t validate_err;
    protecc_net_rule_t copy;

    if (!builder || !rule) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    validate_err = __validate_net_rule(rule);
    if (validate_err != PROTECC_OK) {
        return validate_err;
    }

    reserve_err = __builder_reserve((void**)&builder->net_rules,
                                    &builder->net_rule_capacity,
                                    builder->net_rule_count + 1,
                                    sizeof(protecc_net_rule_t));
    if (reserve_err != PROTECC_OK) {
        return reserve_err;
    }

    copy = *rule;
    copy.ip_pattern = __dup_string(rule->ip_pattern);
    copy.unix_path_pattern = __dup_string(rule->unix_path_pattern);

    if ((rule->ip_pattern && !copy.ip_pattern) ||
        (rule->unix_path_pattern && !copy.unix_path_pattern)) {
        free((void*)copy.ip_pattern);
        free((void*)copy.unix_path_pattern);
        return PROTECC_ERROR_OUT_OF_MEMORY;
    }

    builder->net_rules[builder->net_rule_count++] = copy;
    return PROTECC_OK;
}

protecc_error_t protecc_profile_builder_add_mount_rule(
    protecc_profile_builder_t*  builder,
    const protecc_mount_rule_t* rule)
{
    protecc_error_t reserve_err;
    protecc_error_t validate_err;
    protecc_mount_rule_t copy;

    if (!builder || !rule) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    validate_err = __validate_mount_rule(rule);
    if (validate_err != PROTECC_OK) {
        return validate_err;
    }

    reserve_err = __builder_reserve((void**)&builder->mount_rules,
                                    &builder->mount_rule_capacity,
                                    builder->mount_rule_count + 1,
                                    sizeof(protecc_mount_rule_t));
    if (reserve_err != PROTECC_OK) {
        return reserve_err;
    }

    copy = *rule;
    copy.source_pattern = __dup_string(rule->source_pattern);
    copy.target_pattern = __dup_string(rule->target_pattern);
    copy.fstype_pattern = __dup_string(rule->fstype_pattern);
    copy.options_pattern = __dup_string(rule->options_pattern);

    if ((rule->source_pattern && !copy.source_pattern) ||
        (rule->target_pattern && !copy.target_pattern) ||
        (rule->fstype_pattern && !copy.fstype_pattern) ||
        (rule->options_pattern && !copy.options_pattern)) {
        free((void*)copy.source_pattern);
        free((void*)copy.target_pattern);
        free((void*)copy.fstype_pattern);
        free((void*)copy.options_pattern);
        return PROTECC_ERROR_OUT_OF_MEMORY;
    }

    builder->mount_rules[builder->mount_rule_count++] = copy;
    return PROTECC_OK;
}

protecc_error_t protecc_profile_builder_add_mount_pattern(
    protecc_profile_builder_t*  builder,
    const protecc_mount_rule_t* rule)
{
    return protecc_profile_builder_add_mount_rule(builder, rule);
}

static void __free_compiled_net_rules(protecc_compiled_t* compiled)
{
    if (!compiled || !compiled->net_rules) {
        return;
    }

    for (size_t i = 0; i < compiled->net_rule_count; i++) {
        free((void*)compiled->net_rules[i].ip_pattern);
        free((void*)compiled->net_rules[i].unix_path_pattern);
    }

    free(compiled->net_rules);
    compiled->net_rules = NULL;
    compiled->net_rule_count = 0;
}

static void __free_compiled_mount_rules(protecc_compiled_t* compiled)
{
    if (!compiled || !compiled->mount_rules) {
        return;
    }

    for (size_t i = 0; i < compiled->mount_rule_count; i++) {
        free((void*)compiled->mount_rules[i].source_pattern);
        free((void*)compiled->mount_rules[i].target_pattern);
        free((void*)compiled->mount_rules[i].fstype_pattern);
        free((void*)compiled->mount_rules[i].options_pattern);
    }

    free(compiled->mount_rules);
    compiled->mount_rules = NULL;
    compiled->mount_rule_count = 0;
}

static protecc_error_t __copy_builder_net_rules(
    const protecc_profile_builder_t* builder,
    protecc_compiled_t*              compiled)
{
    if (!builder || !compiled) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    if (builder->net_rule_count == 0) {
        return PROTECC_OK;
    }

    compiled->net_rules = calloc(builder->net_rule_count, sizeof(protecc_net_rule_t));
    if (!compiled->net_rules) {
        return PROTECC_ERROR_OUT_OF_MEMORY;
    }

    compiled->net_rule_count = builder->net_rule_count;
    for (size_t i = 0; i < builder->net_rule_count; i++) {
        compiled->net_rules[i] = builder->net_rules[i];
        compiled->net_rules[i].ip_pattern = __dup_string(builder->net_rules[i].ip_pattern);
        compiled->net_rules[i].unix_path_pattern = __dup_string(builder->net_rules[i].unix_path_pattern);

        if ((builder->net_rules[i].ip_pattern && !compiled->net_rules[i].ip_pattern)
            || (builder->net_rules[i].unix_path_pattern && !compiled->net_rules[i].unix_path_pattern)) {
            __free_compiled_net_rules(compiled);
            return PROTECC_ERROR_OUT_OF_MEMORY;
        }
    }

    return PROTECC_OK;
}

static protecc_error_t __copy_builder_mount_rules(
    const protecc_profile_builder_t* builder,
    protecc_compiled_t*              compiled)
{
    if (!builder || !compiled) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    if (builder->mount_rule_count == 0) {
        return PROTECC_OK;
    }

    compiled->mount_rules = calloc(builder->mount_rule_count, sizeof(protecc_mount_rule_t));
    if (!compiled->mount_rules) {
        return PROTECC_ERROR_OUT_OF_MEMORY;
    }

    compiled->mount_rule_count = builder->mount_rule_count;
    for (size_t i = 0; i < builder->mount_rule_count; i++) {
        compiled->mount_rules[i] = builder->mount_rules[i];
        compiled->mount_rules[i].source_pattern = __dup_string(builder->mount_rules[i].source_pattern);
        compiled->mount_rules[i].target_pattern = __dup_string(builder->mount_rules[i].target_pattern);
        compiled->mount_rules[i].fstype_pattern = __dup_string(builder->mount_rules[i].fstype_pattern);
        compiled->mount_rules[i].options_pattern = __dup_string(builder->mount_rules[i].options_pattern);

        if ((builder->mount_rules[i].source_pattern && !compiled->mount_rules[i].source_pattern)
            || (builder->mount_rules[i].target_pattern && !compiled->mount_rules[i].target_pattern)
            || (builder->mount_rules[i].fstype_pattern && !compiled->mount_rules[i].fstype_pattern)
            || (builder->mount_rules[i].options_pattern && !compiled->mount_rules[i].options_pattern)) {
            __free_compiled_mount_rules(compiled);
            return PROTECC_ERROR_OUT_OF_MEMORY;
        }
    }

    return PROTECC_OK;
}

static protecc_error_t __compile_path_domain(
    const protecc_profile_builder_t* builder,
    protecc_compiled_t*              compiled,
    uint32_t                         flags)
{
    protecc_error_t build_err;
    protecc_error_t finalize_err;

    if (!builder || !compiled) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    if (builder->pattern_count == 0) {
        return PROTECC_OK;
    }

    build_err = __build_trie_patterns(compiled, builder->patterns, builder->pattern_count, flags);
    if (build_err != PROTECC_OK) {
        return build_err;
    }

    finalize_err = __finalize_compilation(compiled);
    if (finalize_err != PROTECC_OK) {
        return finalize_err;
    }

    return PROTECC_OK;
}

static protecc_error_t __compile_net_domain(
    const protecc_profile_builder_t* builder,
    protecc_compiled_t*              compiled)
{
    if (!builder || !compiled) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    return __copy_builder_net_rules(builder, compiled);
}

static protecc_error_t __compile_mount_domain(
    const protecc_profile_builder_t* builder,
    protecc_compiled_t*              compiled)
{
    if (!builder || !compiled) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    return __copy_builder_mount_rules(builder, compiled);
}

static protecc_error_t __resolve_compile_config(
    const protecc_compile_config_t* input,
    protecc_compile_config_t* local,
    const protecc_compile_config_t** cfg_out)
{
    if (!local || !cfg_out) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    if (input == NULL) {
        protecc_compile_config_default(local);
        *cfg_out = local;
    } else {
        *cfg_out = input;
    }

    if ((*cfg_out)->mode != PROTECC_COMPILE_MODE_TRIE && (*cfg_out)->mode != PROTECC_COMPILE_MODE_DFA) {
        return PROTECC_ERROR_COMPILE_FAILED;
    }

    if ((*cfg_out)->max_patterns == 0 || (*cfg_out)->max_pattern_length == 0 ||
        (*cfg_out)->max_states == 0 || (*cfg_out)->max_classes == 0) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    return PROTECC_OK;
}

static protecc_error_t __validate_compile_inputs(
    const protecc_pattern_t*        patterns,
    size_t                          count,
    const protecc_compile_config_t* cfg)
{
    if (!patterns || !cfg || count == 0) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    if (count > cfg->max_patterns) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    for (size_t i = 0; i < count; i++) {
        size_t pattern_length;

        if (patterns[i].pattern == NULL) {
            return PROTECC_ERROR_INVALID_ARGUMENT;
        }

        pattern_length = strlen(patterns[i].pattern);
        if (pattern_length > cfg->max_pattern_length) {
            return PROTECC_ERROR_INVALID_ARGUMENT;
        }
    }

    return PROTECC_OK;
}

static protecc_error_t __build_trie_patterns(
    protecc_compiled_t*             comp,
    const protecc_pattern_t*        patterns,
    size_t                          count,
    uint32_t                        flags)
{
    if (!comp || !patterns) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    comp->root = protecc_node_new(NODE_LITERAL);
    if (!comp->root) {
        return PROTECC_ERROR_OUT_OF_MEMORY;
    }

    for (size_t i = 0; i < count; i++) {
        protecc_node_t* terminal = NULL;
        protecc_error_t err = protecc_parse_pattern(patterns[i].pattern, comp->root, flags, &terminal);
        if (err != PROTECC_OK) {
            return err;
        }
        if (terminal) {
            terminal->perms |= patterns[i].perms;
        }
    }
    return PROTECC_OK;
}

static protecc_error_t __finalize_compilation(protecc_compiled_t* comp) {
    protecc_error_t stats_err;

    if (!comp) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    stats_err = protecc_update_stats(comp);
    if (stats_err != PROTECC_OK) {
        return stats_err;
    }

    if (comp->stats.num_nodes > comp->config.max_states) {
        return PROTECC_ERROR_COMPILE_FAILED;
    }

    if (comp->config.mode == PROTECC_COMPILE_MODE_DFA) {
        return protecc_dfa_from_trie(comp);
    }

    return PROTECC_OK;
}

protecc_error_t protecc_compile(
    const protecc_pattern_t*        patterns,
    size_t                          count,
    uint32_t                        flags,
    const protecc_compile_config_t* config,
    protecc_compiled_t**            compiled
) {
    protecc_profile_builder_t* builder;
    protecc_error_t add_err;
    protecc_error_t compile_err;

    if (!patterns || count == 0) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    builder = protecc_profile_builder_create();
    if (!builder) {
        return PROTECC_ERROR_OUT_OF_MEMORY;
    }

    add_err = protecc_profile_builder_add_patterns(builder, patterns, count);
    if (add_err != PROTECC_OK) {
        protecc_profile_builder_destroy(builder);
        return add_err;
    }

    compile_err = protecc_profile_compile(builder, flags, config, compiled);
    protecc_profile_builder_destroy(builder);
    return compile_err;
}

protecc_error_t protecc_profile_compile(
    const protecc_profile_builder_t* builder,
    uint32_t                         flags,
    const protecc_compile_config_t*  config,
    protecc_compiled_t**             compiled)
{
    protecc_compile_config_t local_config;
    const protecc_compile_config_t* cfg;
    protecc_error_t config_err;
    protecc_error_t input_err;
    protecc_error_t path_err;
    protecc_error_t net_err;
    protecc_error_t mount_err;
    protecc_compiled_t* comp;

    if (!builder || compiled == NULL) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    if (builder->pattern_count == 0
        && builder->net_rule_count == 0
        && builder->mount_rule_count == 0) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    config_err = __resolve_compile_config(config, &local_config, &cfg);
    if (config_err != PROTECC_OK) {
        return config_err;
    }

    if (builder->pattern_count > 0) {
        input_err = __validate_compile_inputs(builder->patterns, builder->pattern_count, cfg);
        if (input_err != PROTECC_OK) {
            return input_err;
        }
    }
    
    comp = calloc(1, sizeof(protecc_compiled_t));
    if (!comp) {
        return PROTECC_ERROR_OUT_OF_MEMORY;
    }

    comp->flags = flags;
    comp->config = *cfg;
    comp->stats.num_patterns = builder->pattern_count;

    path_err = __compile_path_domain(builder, comp, flags);
    if (path_err != PROTECC_OK) {
        protecc_free(comp);
        return path_err;
    }

    net_err = __compile_net_domain(builder, comp);
    if (net_err != PROTECC_OK) {
        protecc_free(comp);
        return net_err;
    }

    mount_err = __compile_mount_domain(builder, comp);
    if (mount_err != PROTECC_OK) {
        protecc_free(comp);
        return mount_err;
    }
    
    *compiled = comp;
    return PROTECC_OK;
}

bool protecc_match(
    const protecc_compiled_t* compiled,
    const char* path,
    size_t path_len,
    protecc_permission_t* permsOut
) {
    if (!permsOut) {
        return false;
    }

    *permsOut = PROTECC_PERM_NONE;

    if (!compiled || !path) {
        return false;
    }
    
    if (path_len == 0) {
        path_len = strlen(path);
    }
    
    if (compiled->has_dfa) {
        return __match_dfa(compiled, path, path_len, permsOut);
    }

    if (!compiled->root) {
        return false;
    }

    return protecc_match_internal(compiled->root, path, path_len, 0, compiled->flags, permsOut);
}

protecc_error_t protecc_get_stats(
    const protecc_compiled_t* compiled,
    protecc_stats_t* stats
) {
    if (!compiled || !stats) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }
    
    *stats = compiled->stats;
    return PROTECC_OK;
}

static void __free_import_nodes(protecc_node_t** nodes, uint32_t count) {
    if (nodes == NULL) {
        return;
    }

    for (uint32_t i = 0; i < count; i++) {
        if (nodes[i]) {
            free(nodes[i]->children);
            free(nodes[i]);
        }
    }
    free(nodes);
}

static protecc_error_t __cleanup_import_trie_failure(
    protecc_node_t** nodes,
    uint32_t num_nodes,
    protecc_compiled_t* comp,
    protecc_error_t error)
{
    __free_import_nodes(nodes, num_nodes);
    free(comp);
    return error;
}

static protecc_error_t __read_and_validate_profile_header(
    const void* buffer,
    size_t buffer_size,
    protecc_profile_header_t* header)
{
    if (!buffer || !header || buffer_size < sizeof(protecc_profile_header_t)) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    memcpy(header, buffer, sizeof(*header));

    if (header->magic != PROTECC_PROFILE_MAGIC || header->version != PROTECC_PROFILE_VERSION) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    if ((header->flags & (PROTECC_PROFILE_FLAG_TYPE_TRIE | PROTECC_PROFILE_FLAG_TYPE_DFA)) == 0) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    return PROTECC_OK;
}

static protecc_error_t __validate_import_dfa_layout(
    const protecc_profile_dfa_t* dfa,
    size_t buffer_size,
    uint32_t header_binary_size,
    size_t* transitions_size,
    size_t* accept_size,
    size_t* perms_size)
{
    size_t transitions_count;
    size_t required_size;

    if (!dfa || !transitions_size || !accept_size || !perms_size) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    if (dfa->num_states == 0 || dfa->num_classes == 0 || dfa->num_classes > 256u) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }
    if (dfa->start_state >= dfa->num_states) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }
    if (dfa->accept_words != ((dfa->num_states + 31u) / 32u)) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    transitions_count = (size_t)dfa->num_states * (size_t)dfa->num_classes;
    *transitions_size = transitions_count * sizeof(uint32_t);
    *accept_size = (size_t)dfa->accept_words * sizeof(uint32_t);
    *perms_size = (size_t)dfa->num_states * sizeof(uint32_t);
    required_size = protecc_profile_dfa_size(dfa->num_states, dfa->num_classes, dfa->accept_words);

    if (buffer_size < required_size || header_binary_size < required_size) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }
    if ((size_t)dfa->classmap_off + PROTECC_PROFILE_DFA_CLASSMAP_SIZE > required_size) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }
    if ((dfa->accept_off & 3u) != 0u || (size_t)dfa->accept_off + *accept_size > required_size) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }
    if ((dfa->perms_off & 3u) != 0u || (size_t)dfa->perms_off + *perms_size > required_size) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }
    if ((dfa->transitions_off & 3u) != 0u || (size_t)dfa->transitions_off + *transitions_size > required_size) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    return PROTECC_OK;
}

static protecc_error_t __allocate_import_dfa_buffers(
    size_t accept_size,
    size_t perms_size,
    size_t transitions_size,
    protecc_compiled_t** compiled_out)
{
    protecc_compiled_t* comp;

    if (!compiled_out) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    comp = calloc(1, sizeof(protecc_compiled_t));
    if (!comp) {
        return PROTECC_ERROR_OUT_OF_MEMORY;
    }

    comp->dfa_accept = malloc(accept_size);
    comp->dfa_perms = malloc(perms_size);
    comp->dfa_transitions = malloc(transitions_size);
    if (!comp->dfa_accept || !comp->dfa_perms || !comp->dfa_transitions) {
        free(comp->dfa_transitions);
        free(comp->dfa_perms);
        free(comp->dfa_accept);
        free(comp);
        return PROTECC_ERROR_OUT_OF_MEMORY;
    }

    *compiled_out = comp;
    return PROTECC_OK;
}

static protecc_error_t __export_dfa_profile(
    const protecc_compiled_t* compiled,
    void*                     buffer,
    size_t                    buffer_size,
    size_t*                   bytes_written)
{
    size_t required_size;
    uint8_t* out;
    protecc_profile_header_t header;
    protecc_profile_dfa_t dfa;
    uint32_t classmap_off;
    uint32_t accept_off;
    uint32_t perms_off;
    uint32_t transitions_off;

    if (!compiled->has_dfa || !compiled->dfa_transitions || !compiled->dfa_accept || !compiled->dfa_perms) {
        return PROTECC_ERROR_COMPILE_FAILED;
    }

    if (compiled->dfa_num_classes != 256u) {
        return PROTECC_ERROR_COMPILE_FAILED;
    }

    required_size = protecc_profile_dfa_size(
        compiled->dfa_num_states,
        compiled->dfa_num_classes,
        compiled->dfa_accept_words);
    if (required_size > UINT32_MAX) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    if (bytes_written) {
        *bytes_written = required_size;
    }

    if (!buffer) {
        return PROTECC_OK;
    }

    if (buffer_size < required_size) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    classmap_off = (uint32_t)(sizeof(protecc_profile_header_t) + sizeof(protecc_profile_dfa_t));
    accept_off = classmap_off + PROTECC_PROFILE_DFA_CLASSMAP_SIZE;
    perms_off = accept_off + (uint32_t)(compiled->dfa_accept_words * sizeof(uint32_t));
    transitions_off = perms_off + (uint32_t)(compiled->dfa_num_states * sizeof(uint32_t));

    memset(&header, 0, sizeof(header));
    header.magic = PROTECC_PROFILE_MAGIC;
    header.version = PROTECC_PROFILE_VERSION;
    header.flags = (compiled->flags & ~(PROTECC_PROFILE_FLAG_TYPE_TRIE | PROTECC_PROFILE_FLAG_TYPE_DFA))
                 | PROTECC_PROFILE_FLAG_TYPE_DFA;
    header.num_nodes = 0;
    header.num_edges = 0;
    header.root_index = 0;
    header.stats.num_patterns = (uint32_t)compiled->stats.num_patterns;
    header.stats.binary_size = (uint32_t)required_size;
    header.stats.max_depth = (uint32_t)compiled->stats.max_depth;
    header.stats.num_nodes = (uint32_t)compiled->stats.num_nodes;

    memset(&dfa, 0, sizeof(dfa));
    dfa.num_states = compiled->dfa_num_states;
    dfa.num_classes = compiled->dfa_num_classes;
    dfa.start_state = compiled->dfa_start_state;
    dfa.accept_words = compiled->dfa_accept_words;
    dfa.classmap_off = classmap_off;
    dfa.accept_off = accept_off;
    dfa.perms_off = perms_off;
    dfa.transitions_off = transitions_off;

    out = (uint8_t*)buffer;
    memcpy(out, &header, sizeof(header));
    memcpy(out + sizeof(header), &dfa, sizeof(dfa));
    memcpy(out + classmap_off, compiled->dfa_classmap, PROTECC_PROFILE_DFA_CLASSMAP_SIZE);
    memcpy(out + accept_off, compiled->dfa_accept, (size_t)compiled->dfa_accept_words * sizeof(uint32_t));
    memcpy(out + perms_off, compiled->dfa_perms, (size_t)compiled->dfa_num_states * sizeof(uint32_t));
    memcpy(out + transitions_off,
           compiled->dfa_transitions,
           (size_t)compiled->dfa_num_states * (size_t)compiled->dfa_num_classes * sizeof(uint32_t));
    return PROTECC_OK;
}

static protecc_error_t __export_trie_profile(
    const protecc_compiled_t* compiled,
    void*                     buffer,
    size_t                    buffer_size,
    size_t*                   bytes_written)
{
    size_t num_edges = 0;
    size_t num_nodes = 0;
    size_t max_depth = 0;
    size_t required_size;
    const protecc_node_t** nodes;
    size_t index;
    uint8_t* out;
    protecc_profile_header_t header;
    protecc_profile_node_t* profile_nodes;
    uint32_t* edges;
    size_t edge_offset;

    if (compiled->stats.num_nodes > UINT32_MAX) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    protecc_node_collect_stats(compiled->root, 0, &num_nodes, &max_depth, &num_edges);
    if (num_edges > UINT32_MAX) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    required_size = protecc_profile_size((uint32_t)num_nodes, (uint32_t)num_edges);
    if (required_size > UINT32_MAX) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    if (bytes_written) {
        *bytes_written = required_size;
    }

    if (!buffer) {
        return PROTECC_OK;
    }

    if (buffer_size < required_size) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    nodes = calloc(num_nodes, sizeof(*nodes));
    if (nodes == NULL) {
        return PROTECC_ERROR_OUT_OF_MEMORY;
    }

    index = 0;
    protecc_collect_nodes(compiled->root, nodes, &index);
    if (index != num_nodes) {
        free(nodes);
        return PROTECC_ERROR_COMPILE_FAILED;
    }

    out = (uint8_t*)buffer;
    header.magic = PROTECC_PROFILE_MAGIC;
    header.version = PROTECC_PROFILE_VERSION;
    header.flags = (compiled->flags & ~(PROTECC_PROFILE_FLAG_TYPE_TRIE | PROTECC_PROFILE_FLAG_TYPE_DFA))
                 | PROTECC_PROFILE_FLAG_TYPE_TRIE;
    header.num_nodes = (uint32_t)num_nodes;
    header.num_edges = (uint32_t)num_edges;
    header.root_index = 0;
    header.stats.num_patterns = (uint32_t)compiled->stats.num_patterns;
    header.stats.binary_size = (uint32_t)required_size;
    header.stats.max_depth = (uint32_t)compiled->stats.max_depth;
    header.stats.num_nodes = (uint32_t)compiled->stats.num_nodes;
    memcpy(out, &header, sizeof(header));

    profile_nodes = (protecc_profile_node_t*)(out + sizeof(header));
    edges = (uint32_t*)(out + sizeof(header) + num_nodes * sizeof(protecc_profile_node_t));

    edge_offset = 0;
    for (size_t i = 0; i < num_nodes; i++) {
        const protecc_node_t* node = nodes[i];
        protecc_profile_node_t profile = {0};
        profile.type = (uint8_t)node->type;
        profile.modifier = (uint8_t)node->modifier;
        profile.is_terminal = node->is_terminal ? 1 : 0;
        profile.child_start = (uint32_t)edge_offset;
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

        profile_nodes[i] = profile;

        for (size_t c = 0; c < node->num_children; c++) {
            size_t child_index = protecc_find_node_index(nodes, num_nodes, node->children[c]);
            if (child_index == SIZE_MAX || child_index > UINT32_MAX) {
                free(nodes);
                return PROTECC_ERROR_COMPILE_FAILED;
            }
            edges[edge_offset++] = (uint32_t)child_index;
        }
    }

    free(nodes);
    return PROTECC_OK;
}

static protecc_error_t __import_dfa_profile(
    const uint8_t*                  base,
    size_t                          buffer_size,
    const protecc_profile_header_t* header,
    protecc_compiled_t**            compiled)
{
    protecc_profile_dfa_t dfa;
    protecc_compiled_t* comp;
    protecc_error_t err;
    size_t transitions_size;
    size_t accept_size;
    size_t perms_size;

    if (buffer_size < sizeof(protecc_profile_header_t) + sizeof(protecc_profile_dfa_t)) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    memcpy(&dfa, base + sizeof(protecc_profile_header_t), sizeof(dfa));

    err = __validate_import_dfa_layout(&dfa, buffer_size, header->stats.binary_size,
                                       &transitions_size, &accept_size, &perms_size);
    if (err != PROTECC_OK) {
        return err;
    }

    err = __allocate_import_dfa_buffers(accept_size, perms_size, transitions_size, &comp);
    if (err != PROTECC_OK) {
        return err;
    }

    memcpy(comp->dfa_classmap, base + dfa.classmap_off, PROTECC_PROFILE_DFA_CLASSMAP_SIZE);
    memcpy(comp->dfa_accept, base + dfa.accept_off, accept_size);
    memcpy(comp->dfa_perms, base + dfa.perms_off, perms_size);
    memcpy(comp->dfa_transitions, base + dfa.transitions_off, transitions_size);

    comp->has_dfa = true;
    comp->dfa_num_states = dfa.num_states;
    comp->dfa_num_classes = dfa.num_classes;
    comp->dfa_start_state = dfa.start_state;
    comp->dfa_accept_words = dfa.accept_words;

    comp->flags = header->flags & ~(PROTECC_PROFILE_FLAG_TYPE_TRIE | PROTECC_PROFILE_FLAG_TYPE_DFA);
    protecc_compile_config_default(&comp->config);
    comp->config.mode = PROTECC_COMPILE_MODE_DFA;
    comp->stats.num_patterns = header->stats.num_patterns;
    comp->stats.binary_size = header->stats.binary_size;
    comp->stats.max_depth = header->stats.max_depth;
    comp->stats.num_nodes = header->stats.num_nodes;

    *compiled = comp;
    return PROTECC_OK;
}

static protecc_error_t __import_trie_profile(
    const uint8_t*                  base,
    size_t                          buffer_size,
    const protecc_profile_header_t* header,
    protecc_compiled_t**            compiled)
{
    size_t required_size = protecc_profile_size(header->num_nodes, header->num_edges);
    const protecc_profile_node_t* profile_nodes;
    const uint32_t* edges;
    protecc_compiled_t* comp;
    protecc_node_t** nodes;

    if (buffer_size < required_size) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    profile_nodes = (const protecc_profile_node_t*)(base + sizeof(protecc_profile_header_t));
    edges = (const uint32_t*)(base + sizeof(protecc_profile_header_t)
                              + (size_t)header->num_nodes * sizeof(protecc_profile_node_t));

    comp = calloc(1, sizeof(protecc_compiled_t));
    if (!comp) {
        return PROTECC_ERROR_OUT_OF_MEMORY;
    }

    if (header->root_index >= header->num_nodes) {
        free(comp);
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    nodes = calloc(header->num_nodes, sizeof(*nodes));
    if (nodes == NULL) {
        free(comp);
        return PROTECC_ERROR_OUT_OF_MEMORY;
    }

    for (uint32_t i = 0; i < header->num_nodes; i++) {
        protecc_node_t* node = protecc_node_new((protecc_node_type_t)profile_nodes[i].type);
        if (node == NULL) {
            return __cleanup_import_trie_failure(nodes, header->num_nodes, comp,
                                                 PROTECC_ERROR_OUT_OF_MEMORY);
        }

        node->modifier = (protecc_modifier_t)profile_nodes[i].modifier;
        node->is_terminal = profile_nodes[i].is_terminal != 0;
        node->perms = (protecc_permission_t)profile_nodes[i].perms;

        switch (node->type) {
            case NODE_LITERAL:
                node->data.literal = (char)profile_nodes[i].data.literal;
                break;
            case NODE_RANGE:
                node->data.range.start = (char)profile_nodes[i].data.range.start;
                node->data.range.end = (char)profile_nodes[i].data.range.end;
                break;
            case NODE_CHARSET:
                memcpy(node->data.charset.chars, profile_nodes[i].data.charset,
                       sizeof(node->data.charset.chars));
                break;
            default:
                break;
        }

        nodes[i] = node;
    }

    for (uint32_t i = 0; i < header->num_nodes; i++) {
        protecc_node_t* node = nodes[i];
        uint16_t child_count = profile_nodes[i].child_count;
        uint32_t child_start = profile_nodes[i].child_start;

        if (child_count == 0) {
            continue;
        }

        if ((uint32_t)child_start + child_count > header->num_edges) {
            return __cleanup_import_trie_failure(nodes, header->num_nodes, comp,
                                                 PROTECC_ERROR_INVALID_ARGUMENT);
        }

        node->children = calloc(child_count, sizeof(*node->children));
        if (node->children == NULL) {
            return __cleanup_import_trie_failure(nodes, header->num_nodes, comp,
                                                 PROTECC_ERROR_OUT_OF_MEMORY);
        }

        node->capacity_children = child_count;
        node->num_children = child_count;
        for (uint16_t c = 0; c < child_count; c++) {
            uint32_t child_index = edges[child_start + c];
            if (child_index >= header->num_nodes) {
                return __cleanup_import_trie_failure(nodes, header->num_nodes, comp,
                                                     PROTECC_ERROR_INVALID_ARGUMENT);
            }
            node->children[c] = nodes[child_index];
        }
    }

    comp->root = nodes[header->root_index];
    comp->flags = header->flags & ~(PROTECC_PROFILE_FLAG_TYPE_TRIE | PROTECC_PROFILE_FLAG_TYPE_DFA);
    protecc_compile_config_default(&comp->config);
    comp->stats.num_patterns = header->stats.num_patterns;
    comp->stats.binary_size = header->stats.binary_size;
    comp->stats.max_depth = header->stats.max_depth;
    comp->stats.num_nodes = header->stats.num_nodes;

    free(nodes);
    *compiled = comp;
    return PROTECC_OK;
}

protecc_error_t protecc_export(
    const protecc_compiled_t* compiled,
    void* buffer,
    size_t buffer_size,
    size_t* bytes_written
) {
    if (!compiled) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    protecc_error_t stats_err = protecc_update_stats((protecc_compiled_t*)compiled);
    if (stats_err != PROTECC_OK) {
        return stats_err;
    }

    if (compiled->stats.num_patterns > UINT32_MAX) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    if (compiled->config.mode == PROTECC_COMPILE_MODE_DFA) {
        return __export_dfa_profile(compiled, buffer, buffer_size, bytes_written);
    }
    return __export_trie_profile(compiled, buffer, buffer_size, bytes_written);
}

protecc_error_t protecc_profile_export_path(
    const protecc_compiled_t* compiled,
    void*                     buffer,
    size_t                    bufferSize,
    size_t*                   bytesWritten)
{
    return protecc_export(compiled, buffer, bufferSize, bytesWritten);
}

static size_t __string_blob_measure(const char* value)
{
    return value ? (strlen(value) + 1u) : 0u;
}

static uint32_t __string_blob_write(uint8_t* base, size_t* cursor, const char* value)
{
    size_t length;

    if (!cursor) {
        return PROTECC_PROFILE_STRING_NONE;
    }

    if (!value) {
        return PROTECC_PROFILE_STRING_NONE;
    }

    length = strlen(value) + 1u;
    if (base) {
        memcpy(base + *cursor, value, length);
    }

    if (*cursor > UINT32_MAX) {
        return PROTECC_PROFILE_STRING_NONE;
    }

    uint32_t offset = (uint32_t)(*cursor);
    *cursor += length;
    return offset;
}

static protecc_error_t __export_net_profile(
    const protecc_compiled_t* compiled,
    void*                     buffer,
    size_t                    buffer_size,
    size_t*                   bytes_written)
{
    size_t strings_size = 0;
    size_t required_size;
    protecc_net_profile_header_t header;

    if (!compiled) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    for (size_t i = 0; i < compiled->net_rule_count; i++) {
        strings_size += __string_blob_measure(compiled->net_rules[i].ip_pattern);
        strings_size += __string_blob_measure(compiled->net_rules[i].unix_path_pattern);
    }

    required_size = sizeof(protecc_net_profile_header_t)
        + (compiled->net_rule_count * sizeof(protecc_net_profile_rule_t))
        + strings_size;

    if (required_size > UINT32_MAX || compiled->net_rule_count > UINT32_MAX || strings_size > UINT32_MAX) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    if (bytes_written) {
        *bytes_written = required_size;
    }

    if (!buffer) {
        return PROTECC_OK;
    }

    if (buffer_size < required_size) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    memset(&header, 0, sizeof(header));
    header.magic = PROTECC_NET_PROFILE_MAGIC;
    header.version = PROTECC_NET_PROFILE_VERSION;
    header.flags = 0;
    header.rule_count = (uint32_t)compiled->net_rule_count;
    header.strings_size = (uint32_t)strings_size;

    memcpy(buffer, &header, sizeof(header));

    {
        uint8_t* out = (uint8_t*)buffer;
        protecc_net_profile_rule_t* out_rules =
            (protecc_net_profile_rule_t*)(out + sizeof(protecc_net_profile_header_t));
        uint8_t* out_strings =
            out + sizeof(protecc_net_profile_header_t)
            + (compiled->net_rule_count * sizeof(protecc_net_profile_rule_t));
        size_t cursor = 0;

        memset(out_rules, 0, compiled->net_rule_count * sizeof(protecc_net_profile_rule_t));

        for (size_t i = 0; i < compiled->net_rule_count; i++) {
            out_rules[i].action = (uint8_t)compiled->net_rules[i].action;
            out_rules[i].protocol = (uint8_t)compiled->net_rules[i].protocol;
            out_rules[i].family = (uint8_t)compiled->net_rules[i].family;
            out_rules[i].port_from = compiled->net_rules[i].port_from;
            out_rules[i].port_to = compiled->net_rules[i].port_to;
            out_rules[i].ip_pattern_off = __string_blob_write(out_strings, &cursor, compiled->net_rules[i].ip_pattern);
            out_rules[i].unix_path_pattern_off = __string_blob_write(out_strings, &cursor, compiled->net_rules[i].unix_path_pattern);
        }

        if (cursor != strings_size) {
            return PROTECC_ERROR_COMPILE_FAILED;
        }
    }

    return PROTECC_OK;
}

static protecc_error_t __export_mount_profile(
    const protecc_compiled_t* compiled,
    void*                     buffer,
    size_t                    buffer_size,
    size_t*                   bytes_written)
{
    size_t strings_size = 0;
    size_t required_size;
    protecc_mount_profile_header_t header;

    if (!compiled) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    for (size_t i = 0; i < compiled->mount_rule_count; i++) {
        strings_size += __string_blob_measure(compiled->mount_rules[i].source_pattern);
        strings_size += __string_blob_measure(compiled->mount_rules[i].target_pattern);
        strings_size += __string_blob_measure(compiled->mount_rules[i].fstype_pattern);
        strings_size += __string_blob_measure(compiled->mount_rules[i].options_pattern);
    }

    required_size = sizeof(protecc_mount_profile_header_t)
        + (compiled->mount_rule_count * sizeof(protecc_mount_profile_rule_t))
        + strings_size;

    if (required_size > UINT32_MAX || compiled->mount_rule_count > UINT32_MAX || strings_size > UINT32_MAX) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    if (bytes_written) {
        *bytes_written = required_size;
    }

    if (!buffer) {
        return PROTECC_OK;
    }

    if (buffer_size < required_size) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    memset(&header, 0, sizeof(header));
    header.magic = PROTECC_MOUNT_PROFILE_MAGIC;
    header.version = PROTECC_MOUNT_PROFILE_VERSION;
    header.flags = 0;
    header.rule_count = (uint32_t)compiled->mount_rule_count;
    header.strings_size = (uint32_t)strings_size;

    memcpy(buffer, &header, sizeof(header));

    {
        uint8_t* out = (uint8_t*)buffer;
        protecc_mount_profile_rule_t* out_rules =
            (protecc_mount_profile_rule_t*)(out + sizeof(protecc_mount_profile_header_t));
        uint8_t* out_strings =
            out + sizeof(protecc_mount_profile_header_t)
            + (compiled->mount_rule_count * sizeof(protecc_mount_profile_rule_t));
        size_t cursor = 0;

        memset(out_rules, 0, compiled->mount_rule_count * sizeof(protecc_mount_profile_rule_t));

        for (size_t i = 0; i < compiled->mount_rule_count; i++) {
            out_rules[i].action = (uint8_t)compiled->mount_rules[i].action;
            out_rules[i].flags = compiled->mount_rules[i].flags;
            out_rules[i].source_pattern_off = __string_blob_write(out_strings, &cursor, compiled->mount_rules[i].source_pattern);
            out_rules[i].target_pattern_off = __string_blob_write(out_strings, &cursor, compiled->mount_rules[i].target_pattern);
            out_rules[i].fstype_pattern_off = __string_blob_write(out_strings, &cursor, compiled->mount_rules[i].fstype_pattern);
            out_rules[i].options_pattern_off = __string_blob_write(out_strings, &cursor, compiled->mount_rules[i].options_pattern);
        }

        if (cursor != strings_size) {
            return PROTECC_ERROR_COMPILE_FAILED;
        }
    }

    return PROTECC_OK;
}

static protecc_error_t __validate_blob_string_offset(
    uint32_t      offset,
    const uint8_t* strings,
    size_t        strings_size)
{
    const uint8_t* end;

    if (offset == PROTECC_PROFILE_STRING_NONE) {
        return PROTECC_OK;
    }

    if (!strings || offset >= strings_size) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    end = memchr(strings + offset, '\0', strings_size - offset);
    if (!end) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    return PROTECC_OK;
}

protecc_error_t protecc_profile_export_net(
    const protecc_compiled_t* compiled,
    void*                     buffer,
    size_t                    bufferSize,
    size_t*                   bytesWritten)
{
    return __export_net_profile(compiled, buffer, bufferSize, bytesWritten);
}

protecc_error_t protecc_profile_export_mounts(
    const protecc_compiled_t* compiled,
    void*                     buffer,
    size_t                    bufferSize,
    size_t*                   bytesWritten)
{
    return __export_mount_profile(compiled, buffer, bufferSize, bytesWritten);
}

protecc_error_t protecc_profile_validate_net_blob(
    const void* buffer,
    size_t      bufferSize)
{
    const uint8_t* base;
    protecc_net_profile_header_t header;
    size_t rules_size;
    size_t required;
    const protecc_net_profile_rule_t* rules;
    const uint8_t* strings;

    if (!buffer || bufferSize < sizeof(protecc_net_profile_header_t)) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    base = (const uint8_t*)buffer;
    memcpy(&header, base, sizeof(header));

    if (header.magic != PROTECC_NET_PROFILE_MAGIC || header.version != PROTECC_NET_PROFILE_VERSION) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    if (header.rule_count > (SIZE_MAX / sizeof(protecc_net_profile_rule_t))) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    rules_size = (size_t)header.rule_count * sizeof(protecc_net_profile_rule_t);
    required = sizeof(protecc_net_profile_header_t) + rules_size + (size_t)header.strings_size;

    if (required < sizeof(protecc_net_profile_header_t) || bufferSize < required) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    rules = (const protecc_net_profile_rule_t*)(base + sizeof(protecc_net_profile_header_t));
    strings = base + sizeof(protecc_net_profile_header_t) + rules_size;

    for (uint32_t i = 0; i < header.rule_count; i++) {
        protecc_action_t action = (protecc_action_t)rules[i].action;
        protecc_net_protocol_t protocol = (protecc_net_protocol_t)rules[i].protocol;
        protecc_net_family_t family = (protecc_net_family_t)rules[i].family;
        protecc_error_t ip_err;
        protecc_error_t unix_err;

        if (!__is_valid_action(action)
            || !__is_valid_net_protocol(protocol)
            || !__is_valid_net_family(family)
            || rules[i].port_from > rules[i].port_to) {
            return PROTECC_ERROR_INVALID_ARGUMENT;
        }

        if (protocol == PROTECC_NET_PROTOCOL_UNIX) {
            if (family == PROTECC_NET_FAMILY_IPV4 || family == PROTECC_NET_FAMILY_IPV6) {
                return PROTECC_ERROR_INVALID_ARGUMENT;
            }
            if (rules[i].port_from != 0 || rules[i].port_to != 0) {
                return PROTECC_ERROR_INVALID_ARGUMENT;
            }
        }

        if (family == PROTECC_NET_FAMILY_UNIX) {
            if (protocol == PROTECC_NET_PROTOCOL_TCP || protocol == PROTECC_NET_PROTOCOL_UDP) {
                return PROTECC_ERROR_INVALID_ARGUMENT;
            }
        }

        ip_err = __validate_blob_string_offset(rules[i].ip_pattern_off, strings, header.strings_size);
        unix_err = __validate_blob_string_offset(rules[i].unix_path_pattern_off, strings, header.strings_size);
        if (ip_err != PROTECC_OK || unix_err != PROTECC_OK) {
            return PROTECC_ERROR_INVALID_ARGUMENT;
        }
    }

    return PROTECC_OK;
}

protecc_error_t protecc_profile_validate_mount_blob(
    const void* buffer,
    size_t      bufferSize)
{
    const uint8_t* base;
    protecc_mount_profile_header_t header;
    size_t rules_size;
    size_t required;
    const protecc_mount_profile_rule_t* rules;
    const uint8_t* strings;

    if (!buffer || bufferSize < sizeof(protecc_mount_profile_header_t)) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    base = (const uint8_t*)buffer;
    memcpy(&header, base, sizeof(header));

    if (header.magic != PROTECC_MOUNT_PROFILE_MAGIC || header.version != PROTECC_MOUNT_PROFILE_VERSION) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    if (header.rule_count > (SIZE_MAX / sizeof(protecc_mount_profile_rule_t))) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    rules_size = (size_t)header.rule_count * sizeof(protecc_mount_profile_rule_t);
    required = sizeof(protecc_mount_profile_header_t) + rules_size + (size_t)header.strings_size;

    if (required < sizeof(protecc_mount_profile_header_t) || bufferSize < required) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    rules = (const protecc_mount_profile_rule_t*)(base + sizeof(protecc_mount_profile_header_t));
    strings = base + sizeof(protecc_mount_profile_header_t) + rules_size;

    for (uint32_t i = 0; i < header.rule_count; i++) {
        protecc_action_t action = (protecc_action_t)rules[i].action;
        protecc_error_t source_err;
        protecc_error_t target_err;
        protecc_error_t fstype_err;
        protecc_error_t options_err;

        if (!__is_valid_action(action)) {
            return PROTECC_ERROR_INVALID_ARGUMENT;
        }

        source_err = __validate_blob_string_offset(rules[i].source_pattern_off, strings, header.strings_size);
        target_err = __validate_blob_string_offset(rules[i].target_pattern_off, strings, header.strings_size);
        fstype_err = __validate_blob_string_offset(rules[i].fstype_pattern_off, strings, header.strings_size);
        options_err = __validate_blob_string_offset(rules[i].options_pattern_off, strings, header.strings_size);

        if (source_err != PROTECC_OK || target_err != PROTECC_OK
            || fstype_err != PROTECC_OK || options_err != PROTECC_OK) {
            return PROTECC_ERROR_INVALID_ARGUMENT;
        }
    }

    return PROTECC_OK;
}

static char* __dup_blob_string(const uint8_t* strings, uint32_t offset)
{
    if (offset == PROTECC_PROFILE_STRING_NONE) {
        return NULL;
    }

    return __dup_string((const char*)(strings + offset));
}

void protecc_profile_free_net_rules(
    protecc_net_rule_t* rules,
    size_t              count)
{
    if (!rules) {
        return;
    }

    for (size_t i = 0; i < count; i++) {
        free((void*)rules[i].ip_pattern);
        free((void*)rules[i].unix_path_pattern);
    }

    free(rules);
}

void protecc_profile_free_mount_rules(
    protecc_mount_rule_t* rules,
    size_t                count)
{
    if (!rules) {
        return;
    }

    for (size_t i = 0; i < count; i++) {
        free((void*)rules[i].source_pattern);
        free((void*)rules[i].target_pattern);
        free((void*)rules[i].fstype_pattern);
        free((void*)rules[i].options_pattern);
    }

    free(rules);
}

static const char* __blob_string_ptr(const uint8_t* strings, uint32_t offset)
{
    if (offset == PROTECC_PROFILE_STRING_NONE) {
        return NULL;
    }

    return (const char*)(strings + offset);
}

protecc_error_t protecc_profile_net_view_init(
    const void*              buffer,
    size_t                   bufferSize,
    protecc_net_blob_view_t* viewOut)
{
    protecc_net_profile_header_t header;

    if (!buffer || !viewOut) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    if (protecc_profile_validate_net_blob(buffer, bufferSize) != PROTECC_OK) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    memcpy(&header, buffer, sizeof(header));

    viewOut->blob = buffer;
    viewOut->blob_size = bufferSize;
    viewOut->rule_count = header.rule_count;
    return PROTECC_OK;
}

protecc_error_t protecc_profile_net_view_get_rule(
    const protecc_net_blob_view_t* view,
    size_t                         index,
    protecc_net_rule_view_t*       ruleOut)
{
    const uint8_t* base;
    protecc_net_profile_header_t header;
    size_t rules_size;
    const protecc_net_profile_rule_t* rules;
    const uint8_t* strings;
    const protecc_net_profile_rule_t* in_rule;

    if (!view || !ruleOut || !view->blob) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    if (index >= view->rule_count) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    if (protecc_profile_validate_net_blob(view->blob, view->blob_size) != PROTECC_OK) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    base = (const uint8_t*)view->blob;
    memcpy(&header, base, sizeof(header));

    rules_size = (size_t)header.rule_count * sizeof(protecc_net_profile_rule_t);
    rules = (const protecc_net_profile_rule_t*)(base + sizeof(protecc_net_profile_header_t));
    strings = base + sizeof(protecc_net_profile_header_t) + rules_size;
    in_rule = &rules[index];

    ruleOut->action = (protecc_action_t)in_rule->action;
    ruleOut->protocol = (protecc_net_protocol_t)in_rule->protocol;
    ruleOut->family = (protecc_net_family_t)in_rule->family;
    ruleOut->port_from = in_rule->port_from;
    ruleOut->port_to = in_rule->port_to;
    ruleOut->ip_pattern = __blob_string_ptr(strings, in_rule->ip_pattern_off);
    ruleOut->unix_path_pattern = __blob_string_ptr(strings, in_rule->unix_path_pattern_off);
    return PROTECC_OK;
}

protecc_error_t protecc_profile_net_view_first(
    const protecc_net_blob_view_t* view,
    size_t*                        iterIndexInOut,
    protecc_net_rule_view_t*       ruleOut)
{
    if (!iterIndexInOut) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    if (!view || view->rule_count == 0) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    *iterIndexInOut = 0;
    return protecc_profile_net_view_get_rule(view, 0, ruleOut);
}

protecc_error_t protecc_profile_net_view_next(
    const protecc_net_blob_view_t* view,
    size_t*                        iterIndexInOut,
    protecc_net_rule_view_t*       ruleOut)
{
    size_t next_index;

    if (!view || !iterIndexInOut) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    next_index = *iterIndexInOut + 1u;
    if (next_index >= view->rule_count) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    *iterIndexInOut = next_index;
    return protecc_profile_net_view_get_rule(view, next_index, ruleOut);
}

protecc_error_t protecc_profile_mount_view_init(
    const void*                buffer,
    size_t                     bufferSize,
    protecc_mount_blob_view_t* viewOut)
{
    protecc_mount_profile_header_t header;

    if (!buffer || !viewOut) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    if (protecc_profile_validate_mount_blob(buffer, bufferSize) != PROTECC_OK) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    memcpy(&header, buffer, sizeof(header));

    viewOut->blob = buffer;
    viewOut->blob_size = bufferSize;
    viewOut->rule_count = header.rule_count;
    return PROTECC_OK;
}

protecc_error_t protecc_profile_mount_view_get_rule(
    const protecc_mount_blob_view_t* view,
    size_t                           index,
    protecc_mount_rule_view_t*       ruleOut)
{
    const uint8_t* base;
    protecc_mount_profile_header_t header;
    size_t rules_size;
    const protecc_mount_profile_rule_t* rules;
    const uint8_t* strings;
    const protecc_mount_profile_rule_t* in_rule;

    if (!view || !ruleOut || !view->blob) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    if (index >= view->rule_count) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    if (protecc_profile_validate_mount_blob(view->blob, view->blob_size) != PROTECC_OK) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    base = (const uint8_t*)view->blob;
    memcpy(&header, base, sizeof(header));

    rules_size = (size_t)header.rule_count * sizeof(protecc_mount_profile_rule_t);
    rules = (const protecc_mount_profile_rule_t*)(base + sizeof(protecc_mount_profile_header_t));
    strings = base + sizeof(protecc_mount_profile_header_t) + rules_size;
    in_rule = &rules[index];

    ruleOut->action = (protecc_action_t)in_rule->action;
    ruleOut->flags = in_rule->flags;
    ruleOut->source_pattern = __blob_string_ptr(strings, in_rule->source_pattern_off);
    ruleOut->target_pattern = __blob_string_ptr(strings, in_rule->target_pattern_off);
    ruleOut->fstype_pattern = __blob_string_ptr(strings, in_rule->fstype_pattern_off);
    ruleOut->options_pattern = __blob_string_ptr(strings, in_rule->options_pattern_off);
    return PROTECC_OK;
}

protecc_error_t protecc_profile_mount_view_first(
    const protecc_mount_blob_view_t* view,
    size_t*                          iterIndexInOut,
    protecc_mount_rule_view_t*       ruleOut)
{
    if (!iterIndexInOut) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    if (!view || view->rule_count == 0) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    *iterIndexInOut = 0;
    return protecc_profile_mount_view_get_rule(view, 0, ruleOut);
}

protecc_error_t protecc_profile_mount_view_next(
    const protecc_mount_blob_view_t* view,
    size_t*                          iterIndexInOut,
    protecc_mount_rule_view_t*       ruleOut)
{
    size_t next_index;

    if (!view || !iterIndexInOut) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    next_index = *iterIndexInOut + 1u;
    if (next_index >= view->rule_count) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    *iterIndexInOut = next_index;
    return protecc_profile_mount_view_get_rule(view, next_index, ruleOut);
}

protecc_error_t protecc_profile_import_net_blob(
    const void*          buffer,
    size_t               bufferSize,
    protecc_net_rule_t** rulesOut,
    size_t*              countOut)
{
    const uint8_t* base;
    protecc_net_profile_header_t header;
    const protecc_net_profile_rule_t* in_rules;
    const uint8_t* strings;
    protecc_net_rule_t* out_rules;
    size_t rules_size;

    if (!buffer || !rulesOut || !countOut) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    *rulesOut = NULL;
    *countOut = 0;

    if (protecc_profile_validate_net_blob(buffer, bufferSize) != PROTECC_OK) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    base = (const uint8_t*)buffer;
    memcpy(&header, base, sizeof(header));

    if (header.rule_count == 0) {
        return PROTECC_OK;
    }

    rules_size = (size_t)header.rule_count * sizeof(protecc_net_profile_rule_t);
    in_rules = (const protecc_net_profile_rule_t*)(base + sizeof(protecc_net_profile_header_t));
    strings = base + sizeof(protecc_net_profile_header_t) + rules_size;

    out_rules = calloc(header.rule_count, sizeof(protecc_net_rule_t));
    if (!out_rules) {
        return PROTECC_ERROR_OUT_OF_MEMORY;
    }

    for (uint32_t i = 0; i < header.rule_count; i++) {
        out_rules[i].action = (protecc_action_t)in_rules[i].action;
        out_rules[i].protocol = (protecc_net_protocol_t)in_rules[i].protocol;
        out_rules[i].family = (protecc_net_family_t)in_rules[i].family;
        out_rules[i].port_from = in_rules[i].port_from;
        out_rules[i].port_to = in_rules[i].port_to;

        out_rules[i].ip_pattern = __dup_blob_string(strings, in_rules[i].ip_pattern_off);
        out_rules[i].unix_path_pattern = __dup_blob_string(strings, in_rules[i].unix_path_pattern_off);

        if ((in_rules[i].ip_pattern_off != PROTECC_PROFILE_STRING_NONE && !out_rules[i].ip_pattern)
            || (in_rules[i].unix_path_pattern_off != PROTECC_PROFILE_STRING_NONE && !out_rules[i].unix_path_pattern)) {
            protecc_profile_free_net_rules(out_rules, i + 1u);
            return PROTECC_ERROR_OUT_OF_MEMORY;
        }
    }

    *rulesOut = out_rules;
    *countOut = header.rule_count;
    return PROTECC_OK;
}

protecc_error_t protecc_profile_import_mount_blob(
    const void*            buffer,
    size_t                 bufferSize,
    protecc_mount_rule_t** rulesOut,
    size_t*                countOut)
{
    const uint8_t* base;
    protecc_mount_profile_header_t header;
    const protecc_mount_profile_rule_t* in_rules;
    const uint8_t* strings;
    protecc_mount_rule_t* out_rules;
    size_t rules_size;

    if (!buffer || !rulesOut || !countOut) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    *rulesOut = NULL;
    *countOut = 0;

    if (protecc_profile_validate_mount_blob(buffer, bufferSize) != PROTECC_OK) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    base = (const uint8_t*)buffer;
    memcpy(&header, base, sizeof(header));

    if (header.rule_count == 0) {
        return PROTECC_OK;
    }

    rules_size = (size_t)header.rule_count * sizeof(protecc_mount_profile_rule_t);
    in_rules = (const protecc_mount_profile_rule_t*)(base + sizeof(protecc_mount_profile_header_t));
    strings = base + sizeof(protecc_mount_profile_header_t) + rules_size;

    out_rules = calloc(header.rule_count, sizeof(protecc_mount_rule_t));
    if (!out_rules) {
        return PROTECC_ERROR_OUT_OF_MEMORY;
    }

    for (uint32_t i = 0; i < header.rule_count; i++) {
        out_rules[i].action = (protecc_action_t)in_rules[i].action;
        out_rules[i].flags = in_rules[i].flags;

        out_rules[i].source_pattern = __dup_blob_string(strings, in_rules[i].source_pattern_off);
        out_rules[i].target_pattern = __dup_blob_string(strings, in_rules[i].target_pattern_off);
        out_rules[i].fstype_pattern = __dup_blob_string(strings, in_rules[i].fstype_pattern_off);
        out_rules[i].options_pattern = __dup_blob_string(strings, in_rules[i].options_pattern_off);

        if ((in_rules[i].source_pattern_off != PROTECC_PROFILE_STRING_NONE && !out_rules[i].source_pattern)
            || (in_rules[i].target_pattern_off != PROTECC_PROFILE_STRING_NONE && !out_rules[i].target_pattern)
            || (in_rules[i].fstype_pattern_off != PROTECC_PROFILE_STRING_NONE && !out_rules[i].fstype_pattern)
            || (in_rules[i].options_pattern_off != PROTECC_PROFILE_STRING_NONE && !out_rules[i].options_pattern)) {
            protecc_profile_free_mount_rules(out_rules, i + 1u);
            return PROTECC_ERROR_OUT_OF_MEMORY;
        }
    }

    *rulesOut = out_rules;
    *countOut = header.rule_count;
    return PROTECC_OK;
}

protecc_error_t protecc_import(
    const void* buffer,
    size_t buffer_size,
    protecc_compiled_t** compiled
) {
    const uint8_t*           base = (const uint8_t*)buffer;
    protecc_profile_header_t header;
    protecc_error_t          header_err;

    if (!compiled || buffer_size < sizeof(uint32_t) * 3) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    header_err = __read_and_validate_profile_header(buffer, buffer_size, &header);
    if (header_err != PROTECC_OK) {
        return header_err;
    }

    if ((header.flags & PROTECC_PROFILE_FLAG_TYPE_DFA) != 0) {
        return __import_dfa_profile(base, buffer_size, &header, compiled);
    }
    return __import_trie_profile(base, buffer_size, &header, compiled);
}

void protecc_free(protecc_compiled_t* compiled) {
    if (!compiled) {
        return;
    }

    __free_compiled_net_rules(compiled);
    __free_compiled_mount_rules(compiled);

    free(compiled->dfa_accept);
    free(compiled->dfa_perms);
    free(compiled->dfa_transitions);
    
    if (compiled->root) {
        protecc_node_free(compiled->root);
    }
    
    free(compiled);
}

protecc_error_t protecc_validate_pattern(const char* pattern) {
    if (!pattern) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }
    
    // Basic validation - check for balanced brackets
    int bracket_depth = 0;
    for (const char* p = pattern; *p; p++) {
        if (*p == '[') {
            bracket_depth++;
        } else if (*p == ']') {
            bracket_depth--;
            if (bracket_depth < 0) {
                return PROTECC_ERROR_INVALID_PATTERN;
            }
        }
    }
    
    if (bracket_depth != 0) {
        return PROTECC_ERROR_INVALID_PATTERN;
    }
    
    return PROTECC_OK;
}
