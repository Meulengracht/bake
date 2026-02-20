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

#include <chef/platform.h>

#include <protecc/profile.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>

#include "../private.h"

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

        pattern_copy = platform_strdup(patterns[i].pattern);
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
    copy.ip_pattern = platform_strdup(rule->ip_pattern);
    copy.unix_path_pattern = platform_strdup(rule->unix_path_pattern);

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
    copy.source_pattern = platform_strdup(rule->source_pattern);
    copy.target_pattern = platform_strdup(rule->target_pattern);
    copy.fstype_pattern = platform_strdup(rule->fstype_pattern);
    copy.options_pattern = platform_strdup(rule->options_pattern);

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

static protecc_error_t __copy_builder_net_rules(
    const protecc_profile_builder_t* builder,
    protecc_profile_t*              compiled)
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
        compiled->net_rules[i].ip_pattern = platform_strdup(builder->net_rules[i].ip_pattern);
        compiled->net_rules[i].unix_path_pattern = platform_strdup(builder->net_rules[i].unix_path_pattern);

        if ((builder->net_rules[i].ip_pattern && !compiled->net_rules[i].ip_pattern)
            || (builder->net_rules[i].unix_path_pattern && !compiled->net_rules[i].unix_path_pattern)) {
            return PROTECC_ERROR_OUT_OF_MEMORY;
        }
    }

    return PROTECC_OK;
}

static protecc_error_t __copy_builder_mount_rules(
    const protecc_profile_builder_t* builder,
    protecc_profile_t*              compiled)
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
        compiled->mount_rules[i].source_pattern = platform_strdup(builder->mount_rules[i].source_pattern);
        compiled->mount_rules[i].target_pattern = platform_strdup(builder->mount_rules[i].target_pattern);
        compiled->mount_rules[i].fstype_pattern = platform_strdup(builder->mount_rules[i].fstype_pattern);
        compiled->mount_rules[i].options_pattern = platform_strdup(builder->mount_rules[i].options_pattern);

        if ((builder->mount_rules[i].source_pattern && !compiled->mount_rules[i].source_pattern)
            || (builder->mount_rules[i].target_pattern && !compiled->mount_rules[i].target_pattern)
            || (builder->mount_rules[i].fstype_pattern && !compiled->mount_rules[i].fstype_pattern)
            || (builder->mount_rules[i].options_pattern && !compiled->mount_rules[i].options_pattern)) {
            return PROTECC_ERROR_OUT_OF_MEMORY;
        }
    }

    return PROTECC_OK;
}

static protecc_error_t __build_trie_patterns(
    protecc_profile_t*             comp,
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

static protecc_error_t __finalize_compilation(protecc_profile_t* comp) {
    protecc_error_t stats_err;

    if (!comp) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    stats_err = __update_stats_trie_profile(comp);
    if (stats_err != PROTECC_OK) {
        return stats_err;
    }

    if (comp->stats.num_nodes > comp->config.max_states) {
        return PROTECC_ERROR_COMPILE_FAILED;
    }

    if (comp->config.mode == PROTECC_COMPILE_MODE_DFA) {
        return protecc_profile_setup_dfa(comp);
    }

    return PROTECC_OK;
}

static protecc_error_t __compile_path_domain(
    const protecc_profile_builder_t* builder,
    protecc_profile_t*              compiled,
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
    protecc_profile_t*              compiled)
{
    if (!builder || !compiled) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    return __copy_builder_net_rules(builder, compiled);
}

static protecc_error_t __compile_mount_domain(
    const protecc_profile_builder_t* builder,
    protecc_profile_t*              compiled)
{
    if (!builder || !compiled) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    return __copy_builder_mount_rules(builder, compiled);
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

protecc_error_t protecc_profile_compile(
    const protecc_profile_builder_t* builder,
    uint32_t                         flags,
    const protecc_compile_config_t*  config,
    protecc_profile_t**             compiled)
{
    protecc_compile_config_t local_config;
    const protecc_compile_config_t* cfg;
    protecc_error_t config_err;
    protecc_error_t input_err;
    protecc_error_t path_err;
    protecc_error_t net_err;
    protecc_error_t mount_err;
    protecc_profile_t* comp;

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
    
    comp = calloc(1, sizeof(protecc_profile_t));
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
