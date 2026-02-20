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

#ifndef __PROTECC_BPF_MOUNT_H__
#define __PROTECC_BPF_MOUNT_H__

#include <protecc/bpf/common.h>

typedef struct {
    __u32                 flags;
    protecc_bpf_string_t  source;
    protecc_bpf_string_t  target;
    protecc_bpf_string_t  fstype;
    protecc_bpf_string_t  options;
} protecc_bpf_mount_request_t;

static __always_inline bool protecc_bpf_match_mount(
    const __u8                       profile[PROTECC_BPF_MAX_PROFILE_SIZE],
    const protecc_bpf_mount_request_t* request,
    __u8*                            action_out)
{
    const protecc_mount_profile_header_t* header;
    const protecc_mount_profile_rule_t*   rules;
    const __u8*                           strings;
    __u64                                 rules_size;
    __u64                                 required_size;
    __u32                                 i;
    bool                                  case_insensitive;

    if (!profile || !request) {
        return false;
    }

    header = (const protecc_mount_profile_header_t*)profile;
    if (header->magic != PROTECC_MOUNT_PROFILE_MAGIC || header->version != PROTECC_MOUNT_PROFILE_VERSION) {
        return false;
    }

    if (header->rule_count > PROTECC_BPF_MAX_MOUNT_RULES) {
        return false;
    }

    rules_size = (__u64)header->rule_count * sizeof(protecc_mount_profile_rule_t);
    required_size = sizeof(protecc_mount_profile_header_t) + rules_size + (__u64)header->strings_size;

    if (required_size > PROTECC_BPF_MAX_PROFILE_SIZE) {
        return false;
    }

    rules = (const protecc_mount_profile_rule_t*)(profile + sizeof(protecc_mount_profile_header_t));
    strings = profile + sizeof(protecc_mount_profile_header_t) + rules_size;

    if (!__VALID_PROFILE_PTR(profile, rules, rules_size)) {
        return false;
    }

    if (!__VALID_PROFILE_PTR(profile, strings, header->strings_size)) {
        return false;
    }

    case_insensitive = (header->flags & PROTECC_PROFILE_FLAG_CASE_INSENSITIVE) != 0;

    bpf_for (i, 0, PROTECC_BPF_MAX_MOUNT_RULES) {
        const protecc_mount_profile_rule_t* rule;

        if (i >= header->rule_count) {
            break;
        }

        rule = &rules[i];

        if (rule->flags != 0 && (request->flags & rule->flags) != rule->flags) {
            continue;
        }

        if (!__protecc_bpf_match_optional(strings,
                                          header->strings_size,
                                          rule->source_pattern_off,
                                          &request->source,
                                          case_insensitive)) {
            continue;
        }

        if (!__protecc_bpf_match_optional(strings,
                                          header->strings_size,
                                          rule->target_pattern_off,
                                          &request->target,
                                          case_insensitive)) {
            continue;
        }

        if (!__protecc_bpf_match_optional(strings,
                                          header->strings_size,
                                          rule->fstype_pattern_off,
                                          &request->fstype,
                                          case_insensitive)) {
            continue;
        }

        if (!__protecc_bpf_match_optional(strings,
                                          header->strings_size,
                                          rule->options_pattern_off,
                                          &request->options,
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

#endif // !__PROTECC_BPF_MOUNT_H__
