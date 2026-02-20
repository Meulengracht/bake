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

#ifndef __PROTECC_BPF_NET_H__
#define __PROTECC_BPF_NET_H__

#include <protecc/bpf/common.h>

#define PROTECC_BPF_NET_PROTOCOL_ANY 0u
#define PROTECC_BPF_NET_FAMILY_ANY   0u

typedef struct {
    __u8                  protocol;
    __u8                  family;
    __u16                 port;
    protecc_bpf_string_t  ip;
    protecc_bpf_string_t  unix_path;
} protecc_bpf_net_request_t;

static __always_inline bool protecc_bpf_match_net(
    const __u8                     profile[PROTECC_BPF_MAX_PROFILE_SIZE],
    const protecc_bpf_net_request_t* request,
    __u8*                          action_out)
{
    const protecc_net_profile_header_t* header;
    const protecc_net_profile_rule_t*   rules;
    const __u8*                         strings;
    __u64                               rules_size;
    __u64                               required_size;
    __u32                               i;
    bool                                case_insensitive;

    if (!profile || !request) {
        return false;
    }

    header = (const protecc_net_profile_header_t*)profile;
    if (header->magic != PROTECC_NET_PROFILE_MAGIC || header->version != PROTECC_NET_PROFILE_VERSION) {
        return false;
    }

    if (header->rule_count > PROTECC_BPF_MAX_NET_RULES) {
        return false;
    }

    rules_size = (__u64)header->rule_count * sizeof(protecc_net_profile_rule_t);
    required_size = sizeof(protecc_net_profile_header_t) + rules_size + (__u64)header->strings_size;

    if (required_size > PROTECC_BPF_MAX_PROFILE_SIZE) {
        return false;
    }

    rules = (const protecc_net_profile_rule_t*)(profile + sizeof(protecc_net_profile_header_t));
    strings = profile + sizeof(protecc_net_profile_header_t) + rules_size;

    if (!__VALID_PROFILE_PTR(profile, rules, rules_size)) {
        return false;
    }

    if (!__VALID_PROFILE_PTR(profile, strings, header->strings_size)) {
        return false;
    }

    case_insensitive = (header->flags & PROTECC_PROFILE_FLAG_CASE_INSENSITIVE) != 0;

    bpf_for (i, 0, PROTECC_BPF_MAX_NET_RULES) {
        const protecc_net_profile_rule_t* rule;

        if (i >= header->rule_count) {
            break;
        }

        rule = &rules[i];

        if (rule->protocol != PROTECC_BPF_NET_PROTOCOL_ANY && rule->protocol != request->protocol) {
            continue;
        }

        if (rule->family != PROTECC_BPF_NET_FAMILY_ANY && rule->family != request->family) {
            continue;
        }

        if (request->port < rule->port_from || request->port > rule->port_to) {
            continue;
        }

        if (!__protecc_bpf_match_optional(strings,
                                          header->strings_size,
                                          rule->ip_pattern_off,
                                          &request->ip,
                                          case_insensitive)) {
            continue;
        }

        if (!__protecc_bpf_match_optional(strings,
                                          header->strings_size,
                                          rule->unix_path_pattern_off,
                                          &request->unix_path,
                                          case_insensitive)) {
            continue;
        }

        if (action_out) {
            *action_out = rule->action;
        }
        return true;
    }

    return false;
}

#endif // !__PROTECC_BPF_NET_H__
