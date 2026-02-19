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

#include "../private.h"

static protecc_error_t __export_mount_profile(
    const protecc_profile_t* compiled,
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
        strings_size += __blob_string_measure(compiled->mount_rules[i].source_pattern);
        strings_size += __blob_string_measure(compiled->mount_rules[i].target_pattern);
        strings_size += __blob_string_measure(compiled->mount_rules[i].fstype_pattern);
        strings_size += __blob_string_measure(compiled->mount_rules[i].options_pattern);
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
            out_rules[i].source_pattern_off = __blob_string_write(out_strings, &cursor, compiled->mount_rules[i].source_pattern);
            out_rules[i].target_pattern_off = __blob_string_write(out_strings, &cursor, compiled->mount_rules[i].target_pattern);
            out_rules[i].fstype_pattern_off = __blob_string_write(out_strings, &cursor, compiled->mount_rules[i].fstype_pattern);
            out_rules[i].options_pattern_off = __blob_string_write(out_strings, &cursor, compiled->mount_rules[i].options_pattern);
        }

        if (cursor != strings_size) {
            return PROTECC_ERROR_COMPILE_FAILED;
        }
    }

    return PROTECC_OK;
}

protecc_error_t protecc_profile_export_mounts(
    const protecc_profile_t* compiled,
    void*                     buffer,
    size_t                    bufferSize,
    size_t*                   bytesWritten)
{
    return __export_mount_profile(compiled, buffer, bufferSize, bytesWritten);
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

        out_rules[i].source_pattern = __blob_string_dup(strings, in_rules[i].source_pattern_off);
        out_rules[i].target_pattern = __blob_string_dup(strings, in_rules[i].target_pattern_off);
        out_rules[i].fstype_pattern = __blob_string_dup(strings, in_rules[i].fstype_pattern_off);
        out_rules[i].options_pattern = __blob_string_dup(strings, in_rules[i].options_pattern_off);

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

protecc_error_t __validate_mount_rule(const protecc_mount_rule_t* rule)
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
