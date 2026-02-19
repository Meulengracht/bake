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
    compiled->mount_rules = NULL;
    compiled->mount_rule_count = 0;
}

static bool __match_dfa(
    const protecc_profile_t* compiled,
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

bool protecc_match_path(
    const protecc_profile_t* compiled,
    const char*              path,
    size_t                   pathLength,
    protecc_permission_t*    permsOut)
{
    if (!permsOut) {
        return false;
    }

    *permsOut = PROTECC_PERM_NONE;

    if (!compiled || !path) {
        return false;
    }
    
    if (pathLength == 0) {
        pathLength = strlen(path);
    }
    
    if (compiled->has_dfa) {
        return __match_dfa(compiled, path, pathLength, permsOut);
    }

    if (!compiled->root) {
        return false;
    }

    return protecc_match_internal(compiled->root, path, pathLength, 0, compiled->flags, permsOut);
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

void protecc_profile_free_net_rules(
    protecc_net_rule_t* rules,
    size_t              count)
{
    if (rules == NULL) {
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
    if (rules == NULL) {
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
