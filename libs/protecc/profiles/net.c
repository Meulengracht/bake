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

static protecc_error_t __validate_blob_string_offset(
    uint32_t       offset,
    const uint8_t* strings,
    size_t         stringsSize)
{
    const uint8_t* end;

    if (offset == PROTECC_PROFILE_STRING_NONE) {
        return PROTECC_OK;
    }

    if (!strings || offset >= stringsSize) {
        return PROTECC_ERROR_INVALID_BLOB;
    }

    end = memchr(strings + offset, '\0', stringsSize - offset);
    if (!end) {
        return PROTECC_ERROR_INVALID_BLOB;
    }

    return PROTECC_OK;
}

static protecc_error_t __export_net_rule(
    const protecc_profile_t* profile,
    void*                    buffer,
    size_t                   stringsSize)
{
    uint8_t*                    out = (uint8_t*)buffer;
    protecc_net_profile_rule_t* rules =
        (protecc_net_profile_rule_t*)(out + sizeof(protecc_net_profile_header_t));
    uint8_t*                    strings =
        out + sizeof(protecc_net_profile_header_t)
        + (profile->net_rule_count * sizeof(protecc_net_profile_rule_t));
    size_t cursor = 0;

    memset(rules, 0, profile->net_rule_count * sizeof(protecc_net_profile_rule_t));

    for (size_t i = 0; i < profile->net_rule_count; i++) {
        rules[i].action = (uint8_t)profile->net_rules[i].action;
        rules[i].protocol = (uint8_t)profile->net_rules[i].protocol;
        rules[i].family = (uint8_t)profile->net_rules[i].family;
        rules[i].port_from = profile->net_rules[i].port_from;
        rules[i].port_to = profile->net_rules[i].port_to;
        rules[i].ip_pattern_off = __blob_string_write(strings, &cursor, profile->net_rules[i].ip_pattern);
        rules[i].unix_path_pattern_off = __blob_string_write(strings, &cursor, profile->net_rules[i].unix_path_pattern);
    }

    if (cursor != stringsSize) {
        return PROTECC_ERROR_COMPILE_FAILED;
    }
    return PROTECC_OK;
}

static protecc_error_t __export_net_profile(
    const protecc_profile_t* profile,
    void*                    buffer,
    size_t                   bufferSize,
    size_t*                  bytesWritten)
{
    protecc_net_profile_header_t header;
    size_t                       stringsSize = 0;
    size_t                       requiredSize;
    protecc_error_t              err;

    for (size_t i = 0; i < profile->net_rule_count; i++) {
        stringsSize += __blob_string_measure(profile->net_rules[i].ip_pattern);
        stringsSize += __blob_string_measure(profile->net_rules[i].unix_path_pattern);
    }

    requiredSize = sizeof(protecc_net_profile_header_t)
        + (profile->net_rule_count * sizeof(protecc_net_profile_rule_t))
        + stringsSize;

    if (requiredSize > UINT32_MAX || profile->net_rule_count > UINT32_MAX || stringsSize > UINT32_MAX) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    if (bytesWritten) {
        *bytesWritten = requiredSize;
    }

    if (!buffer) {
        return PROTECC_OK;
    }

    if (bufferSize < requiredSize) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    memset(&header, 0, sizeof(header));
    header.magic = PROTECC_NET_PROFILE_MAGIC;
    header.version = PROTECC_NET_PROFILE_VERSION;
    header.flags = 0;
    header.rule_count = (uint32_t)profile->net_rule_count;
    header.strings_size = (uint32_t)stringsSize;

    memcpy(buffer, &header, sizeof(header));

    err = __export_net_rule(profile, buffer, stringsSize);
    if (err != PROTECC_OK) {
        return err;
    }
    return PROTECC_OK;
}

protecc_error_t protecc_profile_export_net(
    const protecc_profile_t* profile,
    void*                    buffer,
    size_t                   bufferSize,
    size_t*                  bytesWritten)
{
    return __export_net_profile(profile, buffer, bufferSize, bytesWritten);
}

protecc_error_t protecc_profile_import_net_blob(
    const void*          buffer,
    size_t               bufferSize,
    protecc_net_rule_t** rulesOut,
    size_t*              countOut)
{
    const uint8_t*                    base;
    protecc_net_profile_header_t      header;
    const protecc_net_profile_rule_t* inRules;
    const uint8_t*                    strings;
    protecc_net_rule_t*               outRules;
    size_t                            rulesSize;
    protecc_error_t                   err;

    if (buffer == NULL || rulesOut == NULL || countOut == NULL) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    err = protecc_profile_validate_net_blob(buffer, bufferSize);
    if (err != PROTECC_OK) {
        return err;
    }

    base = (const uint8_t*)buffer;
    memcpy(&header, base, sizeof(header));

    if (header.rule_count == 0) {
        return PROTECC_OK;
    }

    rulesSize = (size_t)header.rule_count * sizeof(protecc_net_profile_rule_t);
    inRules = (const protecc_net_profile_rule_t*)(base + sizeof(protecc_net_profile_header_t));
    strings = base + sizeof(protecc_net_profile_header_t) + rulesSize;

    outRules = calloc(header.rule_count, sizeof(protecc_net_rule_t));
    if (!outRules) {
        return PROTECC_ERROR_OUT_OF_MEMORY;
    }

    for (uint32_t i = 0; i < header.rule_count; i++) {
        outRules[i].action = (protecc_action_t)inRules[i].action;
        outRules[i].protocol = (protecc_net_protocol_t)inRules[i].protocol;
        outRules[i].family = (protecc_net_family_t)inRules[i].family;
        outRules[i].port_from = inRules[i].port_from;
        outRules[i].port_to = inRules[i].port_to;

        outRules[i].ip_pattern = __blob_string_dup(strings, inRules[i].ip_pattern_off);
        outRules[i].unix_path_pattern = __blob_string_dup(strings, inRules[i].unix_path_pattern_off);

        if ((inRules[i].ip_pattern_off != PROTECC_PROFILE_STRING_NONE && !outRules[i].ip_pattern)
            || (inRules[i].unix_path_pattern_off != PROTECC_PROFILE_STRING_NONE && !outRules[i].unix_path_pattern)) {
            protecc_profile_free_net_rules(outRules, i + 1u);
            return PROTECC_ERROR_OUT_OF_MEMORY;
        }
    }

    *rulesOut = outRules;
    *countOut = header.rule_count;
    return PROTECC_OK;
}

protecc_error_t protecc_profile_validate_net_blob(
    const void* buffer,
    size_t      bufferSize)
{
    const uint8_t*                    base;
    protecc_net_profile_header_t      header;
    size_t                            rulesSize;
    size_t                            required;
    const protecc_net_profile_rule_t* rules;
    const uint8_t*                    strings;

    if (buffer == NULL || bufferSize < sizeof(protecc_net_profile_header_t)) {
        return PROTECC_ERROR_INVALID_BLOB;
    }

    base = (const uint8_t*)buffer;
    memcpy(&header, base, sizeof(header));

    if (header.magic != PROTECC_NET_PROFILE_MAGIC || header.version != PROTECC_NET_PROFILE_VERSION) {
        return PROTECC_ERROR_INVALID_BLOB;
    }

    if (header.rule_count > (SIZE_MAX / sizeof(protecc_net_profile_rule_t))) {
        return PROTECC_ERROR_INVALID_BLOB;
    }

    rulesSize = (size_t)header.rule_count * sizeof(protecc_net_profile_rule_t);
    required = sizeof(protecc_net_profile_header_t) + rulesSize + (size_t)header.strings_size;

    if (required < sizeof(protecc_net_profile_header_t) || bufferSize < required) {
        return PROTECC_ERROR_INVALID_BLOB;
    }

    rules = (const protecc_net_profile_rule_t*)(base + sizeof(protecc_net_profile_header_t));
    strings = base + sizeof(protecc_net_profile_header_t) + rulesSize;

    for (uint32_t i = 0; i < header.rule_count; i++) {
        protecc_action_t       action = (protecc_action_t)rules[i].action;
        protecc_net_protocol_t protocol = (protecc_net_protocol_t)rules[i].protocol;
        protecc_net_family_t   family = (protecc_net_family_t)rules[i].family;
        protecc_error_t        err;

        if (!__is_valid_action(action)
            || !__is_valid_net_protocol(protocol)
            || !__is_valid_net_family(family)
            || rules[i].port_from > rules[i].port_to) {
            return PROTECC_ERROR_INVALID_BLOB;
        }

        if (protocol == PROTECC_NET_PROTOCOL_UNIX) {
            if (family == PROTECC_NET_FAMILY_IPV4 || family == PROTECC_NET_FAMILY_IPV6) {
                return PROTECC_ERROR_INVALID_BLOB;
            }
            if (rules[i].port_from != 0 || rules[i].port_to != 0) {
                return PROTECC_ERROR_INVALID_BLOB;
            }
        }

        if (family == PROTECC_NET_FAMILY_UNIX) {
            if (protocol == PROTECC_NET_PROTOCOL_TCP || protocol == PROTECC_NET_PROTOCOL_UDP) {
                return PROTECC_ERROR_INVALID_BLOB;
            }
        }

        err = __validate_blob_string_offset(rules[i].ip_pattern_off, strings, header.strings_size);
        if (err != PROTECC_OK) {
            return err;
        }

        err = __validate_blob_string_offset(rules[i].unix_path_pattern_off, strings, header.strings_size);
        if (err != PROTECC_OK) {
            return err;
        }
    }

    return PROTECC_OK;
}

protecc_error_t protecc_profile_validate_mount_blob(
    const void* buffer,
    size_t      bufferSize)
{
    const uint8_t*                      base;
    protecc_mount_profile_header_t      header;
    size_t                              rulesSize;
    size_t                              required;
    const protecc_mount_profile_rule_t* rules;
    const uint8_t*                      strings;

    if (buffer == NULL || bufferSize < sizeof(protecc_mount_profile_header_t)) {
        return PROTECC_ERROR_INVALID_BLOB;
    }

    base = (const uint8_t*)buffer;
    memcpy(&header, base, sizeof(header));

    if (header.magic != PROTECC_MOUNT_PROFILE_MAGIC || header.version != PROTECC_MOUNT_PROFILE_VERSION) {
        return PROTECC_ERROR_INVALID_BLOB;
    }

    if (header.rule_count > (SIZE_MAX / sizeof(protecc_mount_profile_rule_t))) {
        return PROTECC_ERROR_INVALID_BLOB;
    }

    rulesSize = (size_t)header.rule_count * sizeof(protecc_mount_profile_rule_t);
    required = sizeof(protecc_mount_profile_header_t) + rulesSize + (size_t)header.strings_size;

    if (required < sizeof(protecc_mount_profile_header_t) || bufferSize < required) {
        return PROTECC_ERROR_INVALID_BLOB;
    }

    rules = (const protecc_mount_profile_rule_t*)(base + sizeof(protecc_mount_profile_header_t));
    strings = base + sizeof(protecc_mount_profile_header_t) + rulesSize;

    for (uint32_t i = 0; i < header.rule_count; i++) {
        protecc_action_t action = (protecc_action_t)rules[i].action;
        protecc_error_t  err;

        if (!__is_valid_action(action)) {
            return PROTECC_ERROR_INVALID_BLOB;
        }

        err = __validate_blob_string_offset(rules[i].source_pattern_off, strings, header.strings_size);
        if (err != PROTECC_OK) {
            return err;
        }

        err = __validate_blob_string_offset(rules[i].target_pattern_off, strings, header.strings_size);
        if (err != PROTECC_OK) {
            return err;
        }
        
        err = __validate_blob_string_offset(rules[i].fstype_pattern_off, strings, header.strings_size);
        if (err != PROTECC_OK) {
            return err;
        }
        
        err = __validate_blob_string_offset(rules[i].options_pattern_off, strings, header.strings_size);
        if (err != PROTECC_OK) {
            return err;
        }
    }

    return PROTECC_OK;
}

protecc_error_t protecc_profile_net_view_init(
    const void*              buffer,
    size_t                   bufferSize,
    protecc_net_blob_view_t* viewOut)
{
    protecc_net_profile_header_t header;
    protecc_error_t              err;

    if (!buffer || !viewOut) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    err = protecc_profile_validate_net_blob(buffer, bufferSize);
    if (err != PROTECC_OK) {
        return err;
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
    const uint8_t*                    base;
    protecc_net_profile_header_t      header;
    size_t                            rulesSize;
    const protecc_net_profile_rule_t* rules;
    const uint8_t*                    strings;
    const protecc_net_profile_rule_t* inRule;
    protecc_error_t                   err;

    if (view == NULL || ruleOut == NULL || view->blob == NULL) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    if (index >= view->rule_count) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    err = protecc_profile_validate_net_blob(view->blob, view->blob_size);
    if (err != PROTECC_OK) {
        return err;
    }

    base = (const uint8_t*)view->blob;
    memcpy(&header, base, sizeof(header));

    rulesSize = (size_t)header.rule_count * sizeof(protecc_net_profile_rule_t);
    rules = (const protecc_net_profile_rule_t*)(base + sizeof(protecc_net_profile_header_t));
    strings = base + sizeof(protecc_net_profile_header_t) + rulesSize;
    inRule = &rules[index];

    ruleOut->action = (protecc_action_t)inRule->action;
    ruleOut->protocol = (protecc_net_protocol_t)inRule->protocol;
    ruleOut->family = (protecc_net_family_t)inRule->family;
    ruleOut->port_from = inRule->port_from;
    ruleOut->port_to = inRule->port_to;
    ruleOut->ip_pattern = __blob_string_ptr(strings, inRule->ip_pattern_off);
    ruleOut->unix_path_pattern = __blob_string_ptr(strings, inRule->unix_path_pattern_off);
    return PROTECC_OK;
}

protecc_error_t protecc_profile_net_view_first(
    const protecc_net_blob_view_t* view,
    size_t*                        iterIndexInOut,
    protecc_net_rule_view_t*       ruleOut)
{
    if (iterIndexInOut == NULL) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    if (view == NULL || view->rule_count == 0) {
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
    size_t nextIndex;

    if (view == NULL || iterIndexInOut == NULL) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    nextIndex = *iterIndexInOut + 1u;
    if (nextIndex >= view->rule_count) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    *iterIndexInOut = nextIndex;
    return protecc_profile_net_view_get_rule(view, nextIndex, ruleOut);
}

protecc_error_t __validate_net_rule(const protecc_net_rule_t* rule)
{
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
