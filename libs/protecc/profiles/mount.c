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

static protecc_error_t __mount_combine_pattern(const protecc_mount_rule_t* rule, char** outCombined)
{
    const char* source = rule->source_pattern ? rule->source_pattern : "**";
    const char* target = rule->target_pattern ? rule->target_pattern : "**";
    size_t      source_len;
    size_t      target_len;
    size_t      combined_len;
    char*       combined;

    source_len = strlen(source);
    target_len = strlen(target);

    if (source_len > PROTECC_MAX_GLOB_STEPS || target_len > PROTECC_MAX_GLOB_STEPS) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    combined_len = source_len + 1u + target_len;
    if (combined_len > PROTECC_MAX_GLOB_STEPS) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    combined = malloc(combined_len + 1u);
    if (combined == NULL) {
        return PROTECC_ERROR_OUT_OF_MEMORY;
    }

    memcpy(combined, source, source_len);
    combined[source_len] = (char)PROTECC_MOUNT_DFA_SEPARATOR;
    memcpy(combined + source_len + 1u, target, target_len);
    combined[combined_len] = '\0';

    *outCombined = combined;
    return PROTECC_OK;
}

static protecc_error_t __protecc_mount_build_dfa_internal(protecc_profile_t* profile)
{
    protecc_rule_dfa_pattern_t patterns[PROTECC_MAX_RULES];
    char*                      combined[PROTECC_MAX_RULES] = {0};
    size_t                     count = profile->mount_rule_count;
    protecc_error_t            err;

    if (count == 0) {
        profile->mount_dfa = NULL;
        return PROTECC_OK;
    }

    if (count > PROTECC_MAX_RULES) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    for (size_t i = 0; i < count; i++) {
        err = __mount_combine_pattern(&profile->mount_rules[i], &combined[i]);
        if (err != PROTECC_OK) {
            goto cleanup;
        }

        patterns[i].pattern = combined[i];
        patterns[i].rule_index = (uint32_t)i;
    }

    err = __build_dfa_from_patterns(patterns, count, profile, &profile->mount_dfa);

cleanup:
    for (size_t i = 0; i < count; i++) {
        free(combined[i]);
    }
    return err;
}

protecc_error_t __protecc_mount_build_dfa(protecc_profile_t* profile)
{
    if (profile == NULL) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    __protecc_mount_free_dfa(profile);
    return __protecc_mount_build_dfa_internal(profile);
}

void __protecc_mount_free_dfa(protecc_profile_t* profile)
{
    if (profile == NULL) {
        return;
    }

    __net_dfa_free_runtime(profile->mount_dfa);
    profile->mount_dfa = NULL;
}

struct protecc_mount_rule_offsets {
    uint32_t source_pattern_off;
    uint32_t target_pattern_off;
    uint32_t fstype_pattern_off;
    uint32_t options_pattern_off;
};

static protecc_error_t __validate_pattern_length(const char* pattern)
{
    size_t len;

    if (pattern == NULL) {
        return PROTECC_OK;
    }

    len = strlen(pattern);
    if (len > PROTECC_MAX_GLOB_STEPS) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    return PROTECC_OK;
}

static protecc_error_t __export_mount_rule(
    const protecc_profile_t* profile,
    void*                    buffer,
    size_t                   stringsSize,
    const struct protecc_mount_rule_offsets* offsets)
{
    uint8_t*                      out = (uint8_t*)buffer;
    protecc_mount_profile_rule_t* rules =
        (protecc_mount_profile_rule_t*)(out + sizeof(protecc_rule_profile_header_t));
    uint8_t*                      strings =
        out + sizeof(protecc_rule_profile_header_t)
        + (profile->mount_rule_count * sizeof(protecc_mount_profile_rule_t));
    size_t                        cursor = 0;

    if (offsets == NULL && profile->mount_rule_count != 0) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    memset(rules, 0, profile->mount_rule_count * sizeof(protecc_mount_profile_rule_t));

    for (size_t i = 0; i < profile->mount_rule_count; i++) {
        uint32_t source_off;
        uint32_t target_off;
        uint32_t fstype_off;
        uint32_t options_off;

        rules[i].action = (uint8_t)profile->mount_rules[i].action;
        rules[i].flags = profile->mount_rules[i].flags;

        source_off = __blob_string_write(strings, &cursor, profile->mount_rules[i].source_pattern);
        target_off = __blob_string_write(strings, &cursor, profile->mount_rules[i].target_pattern);
        fstype_off = __blob_string_write(strings, &cursor, profile->mount_rules[i].fstype_pattern);
        options_off = __blob_string_write(strings, &cursor, profile->mount_rules[i].options_pattern);

        if (source_off != offsets[i].source_pattern_off
            || target_off != offsets[i].target_pattern_off
            || fstype_off != offsets[i].fstype_pattern_off
            || options_off != offsets[i].options_pattern_off) {
            return PROTECC_ERROR_COMPILE_FAILED;
        }

        rules[i].source_pattern_off = source_off;
        rules[i].target_pattern_off = target_off;
        rules[i].fstype_pattern_off = fstype_off;
        rules[i].options_pattern_off = options_off;
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
    protecc_rule_profile_header_t   header;
    protecc_rule_dfa_runtime_t*     dfa;
    struct protecc_mount_rule_offsets* offsets = NULL;
    protecc_charclass_table_t        charclasses = {0};
    size_t                           stringsSize = 0;
    size_t                           requiredSize;
    size_t                           rulesSize;
    size_t                           classTableSize;
    size_t                           dfa_size = 0;
    bool                             caseInsensitive;
    protecc_error_t                  err;

    if (profile == NULL) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    dfa = profile->mount_dfa;

    caseInsensitive = (profile->flags & PROTECC_FLAG_CASE_INSENSITIVE) != 0;
    rulesSize = profile->mount_rule_count * sizeof(protecc_mount_profile_rule_t);

    if (profile->mount_rule_count > UINT32_MAX) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    if (profile->mount_rule_count > 0) {
        offsets = calloc(profile->mount_rule_count, sizeof(*offsets));
        if (offsets == NULL) {
            return PROTECC_ERROR_OUT_OF_MEMORY;
        }
    }

    for (size_t i = 0; i < profile->mount_rule_count; i++) {
        uint32_t source_off = PROTECC_PROFILE_STRING_NONE;
        uint32_t target_off = PROTECC_PROFILE_STRING_NONE;
        uint32_t fstype_off = PROTECC_PROFILE_STRING_NONE;
        uint32_t options_off = PROTECC_PROFILE_STRING_NONE;
        size_t   measurement;
        protecc_error_t length_err;

        length_err = __validate_pattern_length(profile->mount_rules[i].source_pattern);
        if (length_err != PROTECC_OK) {
            err = length_err;
            goto cleanup;
        }

        length_err = __validate_pattern_length(profile->mount_rules[i].target_pattern);
        if (length_err != PROTECC_OK) {
            err = length_err;
            goto cleanup;
        }

        length_err = __validate_pattern_length(profile->mount_rules[i].fstype_pattern);
        if (length_err != PROTECC_OK) {
            err = length_err;
            goto cleanup;
        }

        length_err = __validate_pattern_length(profile->mount_rules[i].options_pattern);
        if (length_err != PROTECC_OK) {
            err = length_err;
            goto cleanup;
        }

        measurement = __blob_string_measure(profile->mount_rules[i].source_pattern);
        if (measurement > 0) {
            if (stringsSize > UINT32_MAX) {
                err = PROTECC_ERROR_INVALID_ARGUMENT;
                goto cleanup;
            }

            source_off = (uint32_t)stringsSize;
            err = __charclass_collect(profile->mount_rules[i].source_pattern,
                                      source_off,
                                      caseInsensitive,
                                      &charclasses);
            if (err != PROTECC_OK) {
                goto cleanup;
            }
            stringsSize += measurement;
        }

        measurement = __blob_string_measure(profile->mount_rules[i].target_pattern);
        if (measurement > 0) {
            if (stringsSize > UINT32_MAX) {
                err = PROTECC_ERROR_INVALID_ARGUMENT;
                goto cleanup;
            }

            target_off = (uint32_t)stringsSize;
            err = __charclass_collect(profile->mount_rules[i].target_pattern,
                                      target_off,
                                      caseInsensitive,
                                      &charclasses);
            if (err != PROTECC_OK) {
                goto cleanup;
            }
            stringsSize += measurement;
        }

        measurement = __blob_string_measure(profile->mount_rules[i].fstype_pattern);
        if (measurement > 0) {
            if (stringsSize > UINT32_MAX) {
                err = PROTECC_ERROR_INVALID_ARGUMENT;
                goto cleanup;
            }

            fstype_off = (uint32_t)stringsSize;
            err = __charclass_collect(profile->mount_rules[i].fstype_pattern,
                                      fstype_off,
                                      caseInsensitive,
                                      &charclasses);
            if (err != PROTECC_OK) {
                goto cleanup;
            }
            stringsSize += measurement;
        }

        measurement = __blob_string_measure(profile->mount_rules[i].options_pattern);
        if (measurement > 0) {
            if (stringsSize > UINT32_MAX) {
                err = PROTECC_ERROR_INVALID_ARGUMENT;
                goto cleanup;
            }

            options_off = (uint32_t)stringsSize;
            err = __charclass_collect(profile->mount_rules[i].options_pattern,
                                      options_off,
                                      caseInsensitive,
                                      &charclasses);
            if (err != PROTECC_OK) {
                goto cleanup;
            }
            stringsSize += measurement;
        }

        offsets[i].source_pattern_off = source_off;
        offsets[i].target_pattern_off = target_off;
        offsets[i].fstype_pattern_off = fstype_off;
        offsets[i].options_pattern_off = options_off;
    }

    if (stringsSize > UINT32_MAX || charclasses.count > UINT32_MAX) {
        err = PROTECC_ERROR_INVALID_ARGUMENT;
        goto cleanup;
    }

    classTableSize = charclasses.count * sizeof(protecc_profile_charclass_entry_t);
    if (classTableSize > UINT32_MAX) {
        err = PROTECC_ERROR_INVALID_ARGUMENT;
        goto cleanup;
    }

    if (profile->mount_rule_count > 0) {
        if (dfa == NULL || !dfa->present) {
            err = PROTECC_ERROR_COMPILE_FAILED;
            goto cleanup;
        }

        dfa_size = __net_dfa_block_size(dfa);
    }

    requiredSize = sizeof(protecc_rule_profile_header_t)
        + rulesSize
        + stringsSize
        + classTableSize
        + dfa_size;

    if (requiredSize > UINT32_MAX) {
        err = PROTECC_ERROR_INVALID_ARGUMENT;
        goto cleanup;
    }

    if (bytesWritten) {
        *bytesWritten = requiredSize;
    }

    if (buffer == NULL) {
        err = PROTECC_OK;
        goto cleanup;
    }

    if (bufferSize < requiredSize) {
        err = PROTECC_ERROR_INVALID_ARGUMENT;
        goto cleanup;
    }

    memset(&header, 0, sizeof(header));
    header.magic = PROTECC_MOUNT_PROFILE_MAGIC;
    header.version = PROTECC_MOUNT_PROFILE_VERSION;
    header.flags = caseInsensitive ? PROTECC_PROFILE_FLAG_CASE_INSENSITIVE : 0u;
    header.rule_count = (uint32_t)profile->mount_rule_count;
    header.strings_size = (uint32_t)stringsSize;
    header.charclass_count = (uint32_t)charclasses.count;
    header.charclass_table_off = (uint32_t)(sizeof(protecc_rule_profile_header_t) + rulesSize + stringsSize);
    header.dfa_section_off = dfa_size > 0
        ? (uint32_t)(header.charclass_table_off + classTableSize)
        : 0u;

    memcpy(buffer, &header, sizeof(header));

    err = __export_mount_rule(profile, buffer, stringsSize, offsets);
    if (err != PROTECC_OK) {
        goto cleanup;
    }

    if (charclasses.count > 0) {
        protecc_profile_charclass_entry_t* out_classes;

        out_classes = (protecc_profile_charclass_entry_t*)((uint8_t*)buffer + header.charclass_table_off);
        memcpy(out_classes, charclasses.entries, charclasses.count * sizeof(protecc_profile_charclass_entry_t));
    }

    if (dfa_size > 0) {
        err = __net_export_dfa_block(dfa, (uint8_t*)buffer, bufferSize, header.dfa_section_off);
        if (err != PROTECC_OK) {
            goto cleanup;
        }
    }

    err = PROTECC_OK;

cleanup:
    __charclass_table_free(&charclasses);
    free(offsets);
    return err;
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
    protecc_rule_profile_header_t      header;
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
    inRules = (const protecc_mount_profile_rule_t*)(base + sizeof(protecc_rule_profile_header_t));
    strings = base + sizeof(protecc_rule_profile_header_t) + rulesSize;

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
    protecc_rule_profile_header_t header;
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
    protecc_rule_profile_header_t      header;
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
    rules = (const protecc_mount_profile_rule_t*)(base + sizeof(protecc_rule_profile_header_t));
    strings = base + sizeof(protecc_rule_profile_header_t) + rulesSize;
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
