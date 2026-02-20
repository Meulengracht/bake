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
    size_t  elementSize)
{
    size_t newCapacity;
    void*  resized;

    if (storage == NULL || capacity == NULL || elementSize == 0) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    if (count <= *capacity) {
        return PROTECC_OK;
    }

    newCapacity = (*capacity == 0) ? 8 : *capacity;
    while (newCapacity < count) {
        if (newCapacity > (SIZE_MAX / 2)) {
            return PROTECC_ERROR_OUT_OF_MEMORY;
        }
        newCapacity *= 2;
    }

    if (newCapacity > (SIZE_MAX / elementSize)) {
        return PROTECC_ERROR_OUT_OF_MEMORY;
    }

    resized = realloc(*storage, newCapacity * elementSize);
    if (resized == NULL) {
        return PROTECC_ERROR_OUT_OF_MEMORY;
    }

    *storage = resized;
    *capacity = newCapacity;
    return PROTECC_OK;
}

static void __free_builder_patterns(protecc_profile_builder_t* builder)
{
    if (builder->patterns == NULL) {
        return;
    }

    for (size_t i = 0; i < builder->pattern_count; i++) {
        free((void*)builder->patterns[i].pattern);
    }
    free(builder->patterns);
}

static void __free_builder_net_rules(protecc_profile_builder_t* builder)
{
    if (builder->net_rules == NULL) {
        return;
    }

    for (size_t i = 0; i < builder->net_rule_count; i++) {
        free((void*)builder->net_rules[i].ip_pattern);
        free((void*)builder->net_rules[i].unix_path_pattern);
    }
    free(builder->net_rules);
}

static void __free_builder_mount_rules(protecc_profile_builder_t* builder)
{
    if (builder->mount_rules == NULL) {
        return;
    }

    for (size_t i = 0; i < builder->mount_rule_count; i++) {
        free((void*)builder->mount_rules[i].source_pattern);
        free((void*)builder->mount_rules[i].target_pattern);
        free((void*)builder->mount_rules[i].fstype_pattern);
        free((void*)builder->mount_rules[i].options_pattern);
    }
    free(builder->mount_rules);
}

protecc_profile_builder_t* protecc_profile_builder_create(void)
{
    return calloc(1, sizeof(protecc_profile_builder_t));
}

void protecc_profile_builder_reset(protecc_profile_builder_t* builder)
{
    if (builder == NULL) {
        return;
    }

    __free_builder_patterns(builder);
    __free_builder_net_rules(builder);
    __free_builder_mount_rules(builder);
    memset(builder, 0, sizeof(protecc_profile_builder_t));
}

void protecc_profile_builder_destroy(protecc_profile_builder_t* builder)
{
    if (builder == NULL) {
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
    protecc_error_t err;

    if (builder == NULL || patterns == NULL || count == 0) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    err = __builder_reserve(
        (void**)&builder->patterns,
        &builder->pattern_capacity,
        builder->pattern_count + count,
        sizeof(protecc_pattern_t)
    );
    if (err != PROTECC_OK) {
        return err;
    }

    for (size_t i = 0; i < count; i++) {
        char* pattern_copy;

        if (patterns[i].pattern == NULL) {
            return PROTECC_ERROR_INVALID_ARGUMENT;
        }

        err = protecc_validate_pattern(patterns[i].pattern);
        if (err != PROTECC_OK) {
            return err;
        }

        pattern_copy = platform_strdup(patterns[i].pattern);
        if (pattern_copy == NULL) {
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
    protecc_net_rule_t copy;
    protecc_error_t    err;

    if (builder == NULL || rule == NULL) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    err = __validate_net_rule(rule);
    if (err != PROTECC_OK) {
        return err;
    }

    err = __builder_reserve((void**)&builder->net_rules,
                                    &builder->net_rule_capacity,
                                    builder->net_rule_count + 1,
                                    sizeof(protecc_net_rule_t));
    if (err != PROTECC_OK) {
        return err;
    }

    memcpy(&copy, rule, sizeof(protecc_net_rule_t));
    copy.ip_pattern = rule->ip_pattern ? platform_strdup(rule->ip_pattern) : NULL;
    copy.unix_path_pattern = rule->unix_path_pattern ? platform_strdup(rule->unix_path_pattern) : NULL;

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
    protecc_error_t      err;
    protecc_mount_rule_t copy;

    if (builder == NULL || rule == NULL) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    err = __validate_mount_rule(rule);
    if (err != PROTECC_OK) {
        return err;
    }

    err = __builder_reserve((void**)&builder->mount_rules,
                                    &builder->mount_rule_capacity,
                                    builder->mount_rule_count + 1,
                                    sizeof(protecc_mount_rule_t));
    if (err != PROTECC_OK) {
        return err;
    }

    memcpy(&copy, rule, sizeof(protecc_mount_rule_t));
    copy.source_pattern = rule->source_pattern ? platform_strdup(rule->source_pattern) : NULL;
    copy.target_pattern = rule->target_pattern ? platform_strdup(rule->target_pattern) : NULL;
    copy.fstype_pattern = rule->fstype_pattern ? platform_strdup(rule->fstype_pattern) : NULL;
    copy.options_pattern = rule->options_pattern ? platform_strdup(rule->options_pattern) : NULL;

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
    protecc_profile_t*               profile)
{
    if (builder == NULL || profile == NULL) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    if (builder->net_rule_count == 0) {
        return PROTECC_OK;
    }

    profile->net_rules = calloc(builder->net_rule_count, sizeof(protecc_net_rule_t));
    if (!profile->net_rules) {
        return PROTECC_ERROR_OUT_OF_MEMORY;
    }

    profile->net_rule_count = builder->net_rule_count;
    for (size_t i = 0; i < builder->net_rule_count; i++) {
        profile->net_rules[i] = builder->net_rules[i];
        profile->net_rules[i].ip_pattern = builder->net_rules[i].ip_pattern
            ? platform_strdup(builder->net_rules[i].ip_pattern)
            : NULL;
        profile->net_rules[i].unix_path_pattern = builder->net_rules[i].unix_path_pattern
            ? platform_strdup(builder->net_rules[i].unix_path_pattern)
            : NULL;

        if ((builder->net_rules[i].ip_pattern && !profile->net_rules[i].ip_pattern)
            || (builder->net_rules[i].unix_path_pattern && !profile->net_rules[i].unix_path_pattern)) {
            return PROTECC_ERROR_OUT_OF_MEMORY;
        }
    }

    return PROTECC_OK;
}

static protecc_error_t __copy_builder_mount_rules(
    const protecc_profile_builder_t* builder,
    protecc_profile_t*               profile)
{
    if (builder == NULL || profile == NULL) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    if (builder->mount_rule_count == 0) {
        return PROTECC_OK;
    }

    profile->mount_rules = calloc(builder->mount_rule_count, sizeof(protecc_mount_rule_t));
    if (!profile->mount_rules) {
        return PROTECC_ERROR_OUT_OF_MEMORY;
    }

    profile->mount_rule_count = builder->mount_rule_count;
    for (size_t i = 0; i < builder->mount_rule_count; i++) {
        memcpy(&profile->mount_rules[i], &builder->mount_rules[i], sizeof(protecc_mount_rule_t));
        profile->mount_rules[i].source_pattern = builder->mount_rules[i].source_pattern
            ? platform_strdup(builder->mount_rules[i].source_pattern)
            : NULL;
        profile->mount_rules[i].target_pattern = builder->mount_rules[i].target_pattern
            ? platform_strdup(builder->mount_rules[i].target_pattern)
            : NULL;
        profile->mount_rules[i].fstype_pattern = builder->mount_rules[i].fstype_pattern
            ? platform_strdup(builder->mount_rules[i].fstype_pattern)
            : NULL;
        profile->mount_rules[i].options_pattern = builder->mount_rules[i].options_pattern
            ? platform_strdup(builder->mount_rules[i].options_pattern)
            : NULL;

        if ((builder->mount_rules[i].source_pattern && !profile->mount_rules[i].source_pattern)
            || (builder->mount_rules[i].target_pattern && !profile->mount_rules[i].target_pattern)
            || (builder->mount_rules[i].fstype_pattern && !profile->mount_rules[i].fstype_pattern)
            || (builder->mount_rules[i].options_pattern && !profile->mount_rules[i].options_pattern)) {
            return PROTECC_ERROR_OUT_OF_MEMORY;
        }
    }

    return PROTECC_OK;
}

static protecc_error_t __build_trie_patterns(
    protecc_profile_t*       profile,
    const protecc_pattern_t* patterns,
    size_t                   count,
    uint32_t                 flags)
{
    if (patterns == NULL) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    profile->root = protecc_node_new(NODE_LITERAL);
    if (profile->root == NULL) {
        return PROTECC_ERROR_OUT_OF_MEMORY;
    }

    for (size_t i = 0; i < count; i++) {
        protecc_node_t* terminal = NULL;
        protecc_error_t err = protecc_parse_pattern(patterns[i].pattern, profile->root, flags, &terminal);
        if (err != PROTECC_OK) {
            return err;
        }
        if (terminal != NULL) {
            terminal->perms |= patterns[i].perms;
        }
    }
    return PROTECC_OK;
}

static protecc_error_t __finalize_compilation(protecc_profile_t* profile)
{
    protecc_error_t err;

    if (profile == NULL) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    err = __update_stats_trie_profile(profile);
    if (err != PROTECC_OK) {
        return err;
    }

    if (profile->stats.num_nodes > profile->config.max_states) {
        return PROTECC_ERROR_COMPILE_FAILED;
    }

    if (profile->config.mode == PROTECC_COMPILE_MODE_DFA) {
        return protecc_profile_setup_dfa(profile);
    }

    return PROTECC_OK;
}

static protecc_error_t __compile_path_domain(
    const protecc_profile_builder_t* builder,
    protecc_profile_t*               profile,
    uint32_t                         flags)
{
    protecc_error_t err;

    if (builder == NULL || profile == NULL) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    if (builder->pattern_count == 0) {
        return PROTECC_OK;
    }

    err = __build_trie_patterns(profile, builder->patterns, builder->pattern_count, flags);
    if (err != PROTECC_OK) {
        return err;
    }

    err = __finalize_compilation(profile);
    if (err != PROTECC_OK) {
        return err;
    }

    return PROTECC_OK;
}

static protecc_error_t __compile_net_domain(
    const protecc_profile_builder_t* builder,
    protecc_profile_t*              profile)
{
    if (builder == NULL || profile == NULL) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }
    return __copy_builder_net_rules(builder, profile);
}

static protecc_error_t __compile_mount_domain(
    const protecc_profile_builder_t* builder,
    protecc_profile_t*              profile)
{
    if (builder == NULL || profile == NULL) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }
    return __copy_builder_mount_rules(builder, profile);
}

static protecc_error_t __validate_compile_inputs(
    const protecc_pattern_t*        patterns,
    size_t                          count,
    const protecc_compile_config_t* cfg)
{
    if (patterns == NULL || cfg == NULL || count == 0) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    if (count > cfg->max_patterns) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    for (size_t i = 0; i < count; i++) {
        size_t length;

        if (patterns[i].pattern == NULL) {
            return PROTECC_ERROR_INVALID_ARGUMENT;
        }

        length = strlen(patterns[i].pattern);
        if (length > cfg->max_pattern_length) {
            return PROTECC_ERROR_INVALID_ARGUMENT;
        }
    }

    return PROTECC_OK;
}

static protecc_error_t __resolve_compile_config(
    const protecc_compile_config_t*  input,
    protecc_compile_config_t*        local,
    const protecc_compile_config_t** configOut)
{
    if (local == NULL || configOut == NULL) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    if (input == NULL) {
        protecc_compile_config_default(local);
        *configOut = local;
    } else {
        *configOut = input;
    }

    if ((*configOut)->mode != PROTECC_COMPILE_MODE_TRIE && (*configOut)->mode != PROTECC_COMPILE_MODE_DFA) {
        return PROTECC_ERROR_COMPILE_FAILED;
    }

    if ((*configOut)->max_patterns == 0 || (*configOut)->max_pattern_length == 0 ||
        (*configOut)->max_states == 0 || (*configOut)->max_classes == 0) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    return PROTECC_OK;
}

protecc_error_t protecc_profile_compile(
    const protecc_profile_builder_t* builder,
    uint32_t                         flags,
    const protecc_compile_config_t*  config,
    protecc_profile_t**              compiled)
{
    protecc_compile_config_t        localConfig;
    const protecc_compile_config_t* cfg;
    protecc_error_t                 err;
    protecc_profile_t*              profile;

    if (builder == NULL || compiled == NULL) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    if (builder->pattern_count == 0
        && builder->net_rule_count == 0
        && builder->mount_rule_count == 0) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    err = __resolve_compile_config(config, &localConfig, &cfg);
    if (err != PROTECC_OK) {
        return err;
    }

    if (builder->pattern_count > 0) {
        err = __validate_compile_inputs(builder->patterns, builder->pattern_count, cfg);
        if (err != PROTECC_OK) {
            return err;
        }
    }
    
    profile = calloc(1, sizeof(protecc_profile_t));
    if (!profile) {
        return PROTECC_ERROR_OUT_OF_MEMORY;
    }

    profile->flags = flags;
    profile->config = *cfg;
    profile->stats.num_patterns = builder->pattern_count;

    err = __compile_path_domain(builder, profile, flags);
    if (err != PROTECC_OK) {
        protecc_free(profile);
        return err;
    }

    err = __compile_net_domain(builder, profile);
    if (err != PROTECC_OK) {
        protecc_free(profile);
        return err;
    }

    err = __compile_mount_domain(builder, profile);
    if (err != PROTECC_OK) {
        protecc_free(profile);
        return err;
    }
    
    *compiled = profile;
    return PROTECC_OK;
}
