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

const char* protecc_error_string(protecc_error_t error)
{
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
        case PROTECC_ERROR_INVALID_BLOB:
            return "Invalid blob";
        default:
            return "Unknown error";
    }
}

void protecc_compile_config_default(protecc_compile_config_t* config)
{
    if (config == NULL) {
        return;
    }

    config->mode = PROTECC_COMPILE_MODE_TRIE;
    config->max_patterns = 256;
    config->max_pattern_length = 128;
    config->max_states = 2048;
    config->max_classes = 32;
}

static bool __net_rule_matches(
    const protecc_profile_t*     profile,
    const protecc_net_rule_t*    rule,
    const protecc_net_request_t* request)
{
    bool caseInsensitive;

    if (rule == NULL ) {
        return false;
    }

    if (rule->protocol != PROTECC_NET_PROTOCOL_ANY && rule->protocol != request->protocol) {
        return false;
    }

    if (rule->family != PROTECC_NET_FAMILY_ANY && rule->family != request->family) {
        return false;
    }

    if (request->port < rule->port_from || request->port > rule->port_to) {
        return false;
    }

    caseInsensitive = (profile->flags & PROTECC_FLAG_CASE_INSENSITIVE) != 0;

    if (!__match_optional_pattern(rule->ip_pattern, request->ip, caseInsensitive)) {
        return false;
    }

    if (!__match_optional_pattern(rule->unix_path_pattern, request->unix_path, caseInsensitive)) {
        return false;
    }

    return true;
}

static bool __mount_rule_matches(
    const protecc_profile_t*       compiled,
    const protecc_mount_rule_t*    rule,
    const protecc_mount_request_t* request)
{
    bool caseInsensitive;

    if (rule == NULL) {
        return false;
    }

    caseInsensitive = (compiled->flags & PROTECC_FLAG_CASE_INSENSITIVE) != 0;

    if (!__match_optional_pattern(rule->source_pattern, request->source, caseInsensitive)) {
        return false;
    }

    if (!__match_optional_pattern(rule->target_pattern, request->target, caseInsensitive)) {
        return false;
    }

    if (!__match_optional_pattern(rule->fstype_pattern, request->fstype, caseInsensitive)) {
        return false;
    }

    if (!__match_optional_pattern(rule->options_pattern, request->options, caseInsensitive)) {
        return false;
    }

    if (rule->flags != 0 && (request->flags & rule->flags) != rule->flags) {
        return false;
    }

    return true;
}

bool protecc_match_path(
    const protecc_profile_t* compiled,
    const char*              path,
    protecc_permission_t     requiredPermissions)
{
    if (compiled == NULL || path == NULL) {
        return false;
    }

    if (compiled->has_dfa) {
        return __matcher_dfa(compiled, path, requiredPermissions);
    }
    return __matcher_trie(compiled->root, path, 0, compiled->flags, requiredPermissions);
}

bool protecc_match_net(
    const protecc_profile_t*     profile,
    const protecc_net_request_t* request,
    protecc_action_t*            actionOut)
{
    if (profile == NULL || request == NULL) {
        return false;
    }

    for (size_t i = 0; i < profile->net_rule_count; i++) {
        const protecc_net_rule_t* rule = &profile->net_rules[i];
        if (__net_rule_matches(profile, rule, request)) {
            if (actionOut) {
                *actionOut = rule->action;
            }
            return true;
        }
    }

    return false;
}

bool protecc_match_mount(
    const protecc_profile_t*       profile,
    const protecc_mount_request_t* request,
    protecc_action_t*              actionOut)
{
    if (profile == NULL || request == NULL) {
        return false;
    }

    for (size_t i = 0; i < profile->mount_rule_count; i++) {
        const protecc_mount_rule_t* rule = &profile->mount_rules[i];
        if (__mount_rule_matches(profile, rule, request)) {
            if (actionOut) {
                *actionOut = rule->action;
            }
            return true;
        }
    }

    return false;
}

protecc_error_t protecc_get_stats(
    const protecc_profile_t* compiled,
    protecc_stats_t*         stats)
{
    if (compiled == NULL || stats == NULL) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    *stats = compiled->stats;
    return PROTECC_OK;
}

static void __free_compiled_mount_rules(protecc_profile_t* compiled)
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
}

static void __free_compiled_net_rules(protecc_profile_t* compiled)
{
    if (!compiled || !compiled->net_rules) {
        return;
    }

    for (size_t i = 0; i < compiled->net_rule_count; i++) {
        free((void*)compiled->net_rules[i].ip_pattern);
        free((void*)compiled->net_rules[i].unix_path_pattern);
    }

    free(compiled->net_rules);
}

void protecc_free(protecc_profile_t* compiled)
{
    if (compiled == NULL) {
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

protecc_error_t protecc_compile_patterns(
    const protecc_pattern_t*        patterns,
    size_t                          count,
    uint32_t                        flags,
    const protecc_compile_config_t* config,
    protecc_profile_t**             profileOut)
{
    protecc_profile_builder_t* builder;
    protecc_error_t            err;

    if (patterns == NULL || count == 0) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    builder = protecc_profile_builder_create();
    if (builder == NULL) {
        return PROTECC_ERROR_OUT_OF_MEMORY;
    }

    err = protecc_profile_builder_add_patterns(builder, patterns, count);
    if (err != PROTECC_OK) {
        goto exit;
    }

    err = protecc_profile_compile(builder, flags, config, profileOut);

exit:
    protecc_profile_builder_destroy(builder);
    return err;
}
