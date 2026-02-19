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

static bool __is_valid_action(protecc_action_t action)
{
    return action == PROTECC_ACTION_ALLOW
        || action == PROTECC_ACTION_DENY
        || action == PROTECC_ACTION_AUDIT;
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

static protecc_error_t __export_net_profile(
    const protecc_profile_t* compiled,
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
        strings_size += __blob_string_measure(compiled->net_rules[i].ip_pattern);
        strings_size += __blob_string_measure(compiled->net_rules[i].unix_path_pattern);
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
            out_rules[i].ip_pattern_off = __blob_string_write(out_strings, &cursor, compiled->net_rules[i].ip_pattern);
            out_rules[i].unix_path_pattern_off = __blob_string_write(out_strings, &cursor, compiled->net_rules[i].unix_path_pattern);
        }

        if (cursor != strings_size) {
            return PROTECC_ERROR_COMPILE_FAILED;
        }
    }

    return PROTECC_OK;
}

protecc_error_t protecc_profile_export_net(
    const protecc_profile_t* compiled,
    void*                     buffer,
    size_t                    bufferSize,
    size_t*                   bytesWritten)
{
    return __export_net_profile(compiled, buffer, bufferSize, bytesWritten);
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

        out_rules[i].ip_pattern = __blob_string_dup(strings, in_rules[i].ip_pattern_off);
        out_rules[i].unix_path_pattern = __blob_string_dup(strings, in_rules[i].unix_path_pattern_off);

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

protecc_error_t __validate_net_rule(const protecc_net_rule_t* rule)
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
