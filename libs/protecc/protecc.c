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
    const protecc_net_rule_t*    rule,
    const protecc_net_request_t* request)
{
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

    if (!__match_optional_pattern(rule->ip_pattern, request->ip)) {
        return false;
    }

    if (!__match_optional_pattern(rule->unix_path_pattern, request->unix_path)) {
        return false;
    }

    return true;
}

static bool __net_match_with_dfa(
    const protecc_profile_t*     profile,
    const protecc_net_request_t* request,
    protecc_action_t*            actionOut)
{
    protecc_rule_dfa_runtime_t* dfa;
    const char*                value;
    uint32_t                   state;
    size_t                     len;

    if (request->protocol == PROTECC_NET_PROTOCOL_UNIX || request->family == PROTECC_NET_FAMILY_UNIX) {
        dfa = profile->net_unix_dfa;
        value = request->unix_path ? request->unix_path : "";
    } else {
        dfa = profile->net_ip_dfa;
        value = request->ip ? request->ip : "";
    }

    if (dfa == NULL || !dfa->present) {
        return false;
    }

    len = strlen(value);
    if (len > PROTECC_MAX_GLOB_STEPS) {
        return false;
    }

    state = dfa->start_state;
    for (size_t i = 0; i < len; i++) {
        uint8_t  c = (uint8_t)value[i];
        uint32_t cls;
        uint64_t index;

        cls = dfa->classmap[c];
        if (cls >= dfa->num_classes) {
            return false;
        }

        index = ((uint64_t)state * (uint64_t)dfa->num_classes) + (uint64_t)cls;
        state = dfa->transitions[index];
        if (state >= dfa->num_states) {
            return false;
        }
    }

    if ((dfa->accept[state >> 5] & (1u << (state & 31u))) == 0u) {
        return false;
    }

    if ((uint64_t)dfa->candidate_index[state] + (uint64_t)dfa->candidate_count[state] > dfa->candidates_total) {
        return false;
    }

    for (uint32_t i = 0; i < dfa->candidate_count[state] && i < PROTECC_MAX_RULES; i++) {
        uint32_t rule_index = dfa->candidates[dfa->candidate_index[state] + i];
        const protecc_net_rule_t* rule;

        if (rule_index >= dfa->rule_count) {
            return false;
        }

        rule = &profile->net_rules[rule_index];

        if (__net_rule_matches(rule, request)) {
            if (actionOut) {
                *actionOut = rule->action;
            }
            return true;
        }
    }

    return false;
}

static bool __mount_match_with_dfa(
    const protecc_profile_t*       profile,
    const protecc_mount_request_t* request,
    protecc_action_t*              actionOut)
{
    protecc_rule_dfa_runtime_t* source_target_dfa;
    protecc_rule_dfa_runtime_t* fstype_dfa;
    protecc_rule_dfa_runtime_t* options_dfa;
    const char*                 source;
    const char*                 target;
    const char*                 fstype;
    const char*                 options;
    size_t                      source_len;
    size_t                      target_len;
    size_t                      combined_len;
    char                        combined[PROTECC_MAX_GLOB_STEPS + 1u];
    uint32_t                    source_target_state;
    uint32_t                    fstype_state = 0;
    uint32_t                    options_state = 0;
    bool                        have_fstype_state = false;
    bool                        have_options_state = false;

    source_target_dfa = profile->mount_dfa;
    fstype_dfa = profile->mount_fstype_dfa;
    options_dfa = profile->mount_options_dfa;

    if (source_target_dfa == NULL || !source_target_dfa->present) {
        return false;
    }

    source = request->source ? request->source : "";
    target = request->target ? request->target : "";
    fstype = request->fstype ? request->fstype : "";
    options = request->options ? request->options : "";
    source_len = strlen(source);
    target_len = strlen(target);

    if (source_len + 1u + target_len > PROTECC_MAX_GLOB_STEPS) {
        return false;
    }

    combined_len = source_len + 1u + target_len;
    memcpy(combined, source, source_len);
    combined[source_len] = (char)PROTECC_MOUNT_DFA_SEPARATOR;
    memcpy(combined + source_len + 1u, target, target_len);
    combined[combined_len] = '\0';

    source_target_state = source_target_dfa->start_state;
    for (size_t i = 0; i < combined_len; i++) {
        uint8_t  c   = (uint8_t)combined[i];
        uint32_t cls = source_target_dfa->classmap[c];
        uint64_t index;

        if (cls >= source_target_dfa->num_classes) {
            return false;
        }

        index = ((uint64_t)source_target_state * (uint64_t)source_target_dfa->num_classes) + (uint64_t)cls;
        source_target_state = source_target_dfa->transitions[index];
        if (source_target_state >= source_target_dfa->num_states) {
            return false;
        }
    }

    if ((source_target_dfa->accept[source_target_state >> 5] & (1u << (source_target_state & 31u))) == 0u) {
        return false;
    }

    if ((uint64_t)source_target_dfa->candidate_index[source_target_state] + (uint64_t)source_target_dfa->candidate_count[source_target_state] > source_target_dfa->candidates_total) {
        return false;
    }

    if (fstype_dfa && fstype_dfa->present) {
        size_t len = strlen(fstype);

        if (len > PROTECC_MAX_GLOB_STEPS) {
            return false;
        }

        fstype_state = fstype_dfa->start_state;
        for (size_t i = 0; i < len; i++) {
            uint8_t  c   = (uint8_t)fstype[i];
            uint32_t cls = fstype_dfa->classmap[c];
            uint64_t index;

            if (cls >= fstype_dfa->num_classes) {
                return false;
            }

            index = ((uint64_t)fstype_state * (uint64_t)fstype_dfa->num_classes) + (uint64_t)cls;
            fstype_state = fstype_dfa->transitions[index];
            if (fstype_state >= fstype_dfa->num_states) {
                return false;
            }
        }

        if ((fstype_dfa->accept[fstype_state >> 5] & (1u << (fstype_state & 31u))) != 0u
            && (uint64_t)fstype_dfa->candidate_index[fstype_state] + (uint64_t)fstype_dfa->candidate_count[fstype_state] <= fstype_dfa->candidates_total) {
            have_fstype_state = true;
        }
    }

    if (options_dfa && options_dfa->present) {
        size_t len = strlen(options);

        if (len > PROTECC_MAX_GLOB_STEPS) {
            return false;
        }

        options_state = options_dfa->start_state;
        for (size_t i = 0; i < len; i++) {
            uint8_t  c   = (uint8_t)options[i];
            uint32_t cls = options_dfa->classmap[c];
            uint64_t index;

            if (cls >= options_dfa->num_classes) {
                return false;
            }

            index = ((uint64_t)options_state * (uint64_t)options_dfa->num_classes) + (uint64_t)cls;
            options_state = options_dfa->transitions[index];
            if (options_state >= options_dfa->num_states) {
                return false;
            }
        }

        if ((options_dfa->accept[options_state >> 5] & (1u << (options_state & 31u))) != 0u
            && (uint64_t)options_dfa->candidate_index[options_state] + (uint64_t)options_dfa->candidate_count[options_state] <= options_dfa->candidates_total) {
            have_options_state = true;
        }
    }

    for (uint32_t i = 0; i < source_target_dfa->candidate_count[source_target_state] && i < PROTECC_MAX_RULES; i++) {
        uint32_t               rule_index = source_target_dfa->candidates[source_target_dfa->candidate_index[source_target_state] + i];
        const protecc_mount_rule_t* rule;
        bool                   fstype_ok = true;
        bool                   options_ok = true;

        if (rule_index >= source_target_dfa->rule_count) {
            return false;
        }

        rule = &profile->mount_rules[rule_index];

        if (rule->flags != 0 && (request->flags & rule->flags) != rule->flags) {
            continue;
        }

        if (rule->fstype_pattern != NULL) {
            if (!have_fstype_state) {
                continue;
            }

            fstype_ok = false;
            for (uint32_t j = 0; j < fstype_dfa->candidate_count[fstype_state] && j < PROTECC_MAX_RULES; j++) {
                uint32_t candidate = fstype_dfa->candidates[fstype_dfa->candidate_index[fstype_state] + j];
                if (candidate == rule_index) {
                    fstype_ok = true;
                    break;
                }
            }
        }

        if (!fstype_ok) {
            continue;
        }

        if (rule->options_pattern != NULL) {
            if (!have_options_state) {
                continue;
            }

            options_ok = false;
            for (uint32_t j = 0; j < options_dfa->candidate_count[options_state] && j < PROTECC_MAX_RULES; j++) {
                uint32_t candidate = options_dfa->candidates[options_dfa->candidate_index[options_state] + j];
                if (candidate == rule_index) {
                    options_ok = true;
                    break;
                }
            }
        }

        if (!options_ok) {
            continue;
        }

        if (actionOut) {
            *actionOut = rule->action;
        }
        return true;
    }

    return false;
}

static bool __mount_rule_matches(
    const protecc_mount_rule_t*    rule,
    const protecc_mount_request_t* request)
{
    if (rule == NULL) {
        return false;
    }

    if (!__match_optional_pattern(rule->source_pattern, request->source)) {
        return false;
    }

    if (!__match_optional_pattern(rule->target_pattern, request->target)) {
        return false;
    }

    if (!__match_optional_pattern(rule->fstype_pattern, request->fstype)) {
        return false;
    }

    if (!__match_optional_pattern(rule->options_pattern, request->options)) {
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

    if (compiled->path_dfa.present) {
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

    if (__net_match_with_dfa(profile, request, actionOut)) {
        return true;
    }

    for (size_t i = 0; i < profile->net_rule_count; i++) {
        const protecc_net_rule_t* rule = &profile->net_rules[i];
        if (__net_rule_matches(rule, request)) {
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

    if (__mount_match_with_dfa(profile, request, actionOut)) {
        return true;
    }

    for (size_t i = 0; i < profile->mount_rule_count; i++) {
        const protecc_mount_rule_t* rule = &profile->mount_rules[i];
        if (__mount_rule_matches(rule, request)) {
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
    if (compiled->mount_rules == NULL) {
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
    if (compiled->net_rules == NULL) {
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
    protecc_path_dfa_runtime_t* dfa;

    if (compiled == NULL) {
        return;
    }

    dfa = &compiled->path_dfa;

    __protecc_net_free_dfas(compiled);
    __protecc_mount_free_dfa(compiled);
    __free_compiled_net_rules(compiled);
    __free_compiled_mount_rules(compiled);

    free(dfa->accept);
    free(dfa->perms);
    free(dfa->transitions);
    
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
