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

static protecc_error_t __export_mount_rule(
    const protecc_profile_t* profile,
    void*                    buffer,
    size_t                   stringsSize)
{
    uint8_t*                      out = (uint8_t*)buffer;
    protecc_mount_profile_rule_t* rules =
        (protecc_mount_profile_rule_t*)(out + sizeof(protecc_mount_profile_header_t));
    uint8_t*                      strings =
        out + sizeof(protecc_mount_profile_header_t)
        + (profile->mount_rule_count * sizeof(protecc_mount_profile_rule_t));
    size_t                        cursor = 0;

    memset(rules, 0, profile->mount_rule_count * sizeof(protecc_mount_profile_rule_t));

    for (size_t i = 0; i < profile->mount_rule_count; i++) {
        rules[i].action = (uint8_t)profile->mount_rules[i].action;
        rules[i].flags = profile->mount_rules[i].flags;
        rules[i].source_pattern_off = __blob_string_write(strings, &cursor, profile->mount_rules[i].source_pattern);
        rules[i].target_pattern_off = __blob_string_write(strings, &cursor, profile->mount_rules[i].target_pattern);
        rules[i].fstype_pattern_off = __blob_string_write(strings, &cursor, profile->mount_rules[i].fstype_pattern);
        rules[i].options_pattern_off = __blob_string_write(strings, &cursor, profile->mount_rules[i].options_pattern);
    }

    if (cursor != stringsSize) {
        return PROTECC_ERROR_COMPILE_FAILED;
    }
    return PROTECC_OK;
}

static protecc_error_t __export_mount_profile(
    const protecc_profile_t* profile,
    void*                    buffer,
    size_t                   bufferSize,
    size_t*                  bytesWritten)
{
    size_t                         stringsSize = 0;
    size_t                         requiredSize;
    protecc_mount_profile_header_t header;
    protecc_error_t                err;

    if (profile == NULL) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    for (size_t i = 0; i < profile->mount_rule_count; i++) {
        stringsSize += __blob_string_measure(profile->mount_rules[i].source_pattern);
        stringsSize += __blob_string_measure(profile->mount_rules[i].target_pattern);
        stringsSize += __blob_string_measure(profile->mount_rules[i].fstype_pattern);
        stringsSize += __blob_string_measure(profile->mount_rules[i].options_pattern);
    }

    requiredSize = sizeof(protecc_mount_profile_header_t)
        + (profile->mount_rule_count * sizeof(protecc_mount_profile_rule_t))
        + stringsSize;

    if (requiredSize > UINT32_MAX || profile->mount_rule_count > UINT32_MAX || stringsSize > UINT32_MAX) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    if (bytesWritten) {
        *bytesWritten = requiredSize;
    }

    if (buffer == NULL) {
        return PROTECC_OK;
    }

    if (bufferSize < requiredSize) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    memset(&header, 0, sizeof(header));
    header.magic = PROTECC_MOUNT_PROFILE_MAGIC;
    header.version = PROTECC_MOUNT_PROFILE_VERSION;
    header.flags = 0;
    header.rule_count = (uint32_t)profile->mount_rule_count;
    header.strings_size = (uint32_t)stringsSize;

    memcpy(buffer, &header, sizeof(header));

    err = __export_mount_rule(profile, buffer, stringsSize);
    if (err != PROTECC_OK) {
        return err;
    }

    return PROTECC_OK;
}

protecc_error_t protecc_profile_export_mounts(
    const protecc_profile_t* profile,
    void*                    buffer,
    size_t                   bufferSize,
    size_t*                  bytesWritten)
{
    return __export_mount_profile(profile, buffer, bufferSize, bytesWritten);
}

protecc_error_t protecc_profile_import_mount_blob(
    const void*            buffer,
    size_t                 bufferSize,
    protecc_mount_rule_t** rulesOut,
    size_t*                countOut)
{
    const uint8_t*                      base;
    protecc_mount_profile_header_t      header;
    const protecc_mount_profile_rule_t* inRules;
    const uint8_t*                      strings;
    protecc_mount_rule_t*               outRules;
    size_t                              rulesSize;
    protecc_error_t                     err;

    if (buffer == NULL || rulesOut == NULL || countOut == NULL) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    err = protecc_profile_validate_mount_blob(buffer, bufferSize);
    if (err != PROTECC_OK) {
        return err;
    }

    base = (const uint8_t*)buffer;
    memcpy(&header, base, sizeof(header));

    if (header.rule_count == 0) {
        return PROTECC_OK;
    }

    rulesSize = (size_t)header.rule_count * sizeof(protecc_mount_profile_rule_t);
    inRules = (const protecc_mount_profile_rule_t*)(base + sizeof(protecc_mount_profile_header_t));
    strings = base + sizeof(protecc_mount_profile_header_t) + rulesSize;

    outRules = calloc(header.rule_count, sizeof(protecc_mount_rule_t));
    if (!outRules) {
        return PROTECC_ERROR_OUT_OF_MEMORY;
    }

    for (uint32_t i = 0; i < header.rule_count; i++) {
        outRules[i].action = (protecc_action_t)inRules[i].action;
        outRules[i].flags = inRules[i].flags;

        outRules[i].source_pattern = __blob_string_dup(strings, inRules[i].source_pattern_off);
        outRules[i].target_pattern = __blob_string_dup(strings, inRules[i].target_pattern_off);
        outRules[i].fstype_pattern = __blob_string_dup(strings, inRules[i].fstype_pattern_off);
        outRules[i].options_pattern = __blob_string_dup(strings, inRules[i].options_pattern_off);

        if ((inRules[i].source_pattern_off != PROTECC_PROFILE_STRING_NONE && !outRules[i].source_pattern)
            || (inRules[i].target_pattern_off != PROTECC_PROFILE_STRING_NONE && !outRules[i].target_pattern)
            || (inRules[i].fstype_pattern_off != PROTECC_PROFILE_STRING_NONE && !outRules[i].fstype_pattern)
            || (inRules[i].options_pattern_off != PROTECC_PROFILE_STRING_NONE && !outRules[i].options_pattern)) {
            protecc_profile_free_mount_rules(outRules, i + 1u);
            return PROTECC_ERROR_OUT_OF_MEMORY;
        }
    }

    *rulesOut = outRules;
    *countOut = header.rule_count;
    return PROTECC_OK;
}

protecc_error_t protecc_profile_mount_view_init(
    const void*                buffer,
    size_t                     bufferSize,
    protecc_mount_blob_view_t* viewOut)
{
    protecc_mount_profile_header_t header;
    protecc_error_t                err;

    if (buffer == NULL || viewOut == NULL) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    err = protecc_profile_validate_mount_blob(buffer, bufferSize);
    if (err != PROTECC_OK) {
        return err;
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
    const uint8_t*                      base;
    protecc_mount_profile_header_t      header;
    size_t                              rulesSize;
    const protecc_mount_profile_rule_t* rules;
    const uint8_t*                      strings;
    const protecc_mount_profile_rule_t* inRule;
    protecc_error_t                     err;

    if (view == NULL || ruleOut == NULL || view->blob == NULL) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    if (index >= view->rule_count) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    err = protecc_profile_validate_mount_blob(view->blob, view->blob_size);
    if (err != PROTECC_OK) {
        return err;
    }

    base = (const uint8_t*)view->blob;
    memcpy(&header, base, sizeof(header));

    rulesSize = (size_t)header.rule_count * sizeof(protecc_mount_profile_rule_t);
    rules = (const protecc_mount_profile_rule_t*)(base + sizeof(protecc_mount_profile_header_t));
    strings = base + sizeof(protecc_mount_profile_header_t) + rulesSize;
    inRule = &rules[index];

    ruleOut->action = (protecc_action_t)inRule->action;
    ruleOut->flags = inRule->flags;
    ruleOut->source_pattern = __blob_string_ptr(strings, inRule->source_pattern_off);
    ruleOut->target_pattern = __blob_string_ptr(strings, inRule->target_pattern_off);
    ruleOut->fstype_pattern = __blob_string_ptr(strings, inRule->fstype_pattern_off);
    ruleOut->options_pattern = __blob_string_ptr(strings, inRule->options_pattern_off);
    return PROTECC_OK;
}

protecc_error_t protecc_profile_mount_view_first(
    const protecc_mount_blob_view_t* view,
    size_t*                          iterIndexInOut,
    protecc_mount_rule_view_t*       ruleOut)
{
    if (iterIndexInOut == NULL) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    if (view == NULL || view->rule_count == 0) {
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
    size_t nextIndex;

    if (view == NULL || iterIndexInOut == NULL) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    nextIndex = *iterIndexInOut + 1u;
    if (nextIndex >= view->rule_count) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    *iterIndexInOut = nextIndex;
    return protecc_profile_mount_view_get_rule(view, nextIndex, ruleOut);
}

protecc_error_t __validate_mount_rule(const protecc_mount_rule_t* rule)
{
    if (!__is_valid_action(rule->action)) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    if (rule->source_pattern) {
        protecc_error_t err = protecc_validate_pattern(rule->source_pattern);
        if (err != PROTECC_OK) {
            return err;
        }
    }

    if (rule->target_pattern) {
        protecc_error_t err = protecc_validate_pattern(rule->target_pattern);
        if (err != PROTECC_OK) {
            return err;
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
