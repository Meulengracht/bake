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

#include <protecc/bpf/match.h>

typedef struct {
    __u32                 flags;
    protecc_bpf_string_t  source;
    protecc_bpf_string_t  target;
    protecc_bpf_string_t  fstype;
    protecc_bpf_string_t  options;
} protecc_bpf_mount_request_t;

static __always_inline bool protecc_bpf_match_mount(
    const __u8                         profile[PROTECC_BPF_MAX_PROFILE_SIZE],
    const protecc_bpf_mount_request_t* request,
    __u8*                              action_out)
{
    const protecc_rule_profile_header_t* header;
    const protecc_mount_profile_rule_t*   rules;
    const __u8*                           strings;
    const protecc_profile_charclass_entry_t* classes;
    const protecc_profile_dfa_t*              dfa;
    const __u8*                           dfa_base;
    const __u8*                           classmap;
    const __u32*                          accept;
    const __u32*                          candidate_index;
    const __u32*                          candidate_count;
    const __u32*                          candidates;
    const __u32*                          transitions;
    __u64                                 rules_size;
    __u64                                 required_size;
    __u64                                 class_table_size;
    __u64                                 dfa_end;
    __u32                                 i;
    bool                                  case_insensitive;

    if (!profile || !request) {
        return false;
    }

    header = (const protecc_rule_profile_header_t*)profile;
    if (header->magic != PROTECC_MOUNT_PROFILE_MAGIC || header->version != PROTECC_MOUNT_PROFILE_VERSION) {
        return false;
    }

    if ((header->flags & ~(PROTECC_PROFILE_FLAG_CASE_INSENSITIVE)) != 0) {
        return false;
    }

    if (header->rule_count > PROTECC_BPF_MAX_MOUNT_RULES) {
        return false;
    }

    if (header->charclass_count > PROTECC_BPF_MAX_CHAR_CLASSES) {
        return false;
    }

    rules_size = (__u64)header->rule_count * sizeof(protecc_mount_profile_rule_t);
    class_table_size = (__u64)header->charclass_count * sizeof(protecc_profile_charclass_entry_t);

    if (header->charclass_table_off < sizeof(protecc_rule_profile_header_t) + rules_size + (__u64)header->strings_size) {
        return false;
    }

    required_size = (__u64)header->charclass_table_off + class_table_size;

    if (required_size > PROTECC_BPF_MAX_PROFILE_SIZE) {
        return false;
    }

    rules = (const protecc_mount_profile_rule_t*)(profile + sizeof(protecc_rule_profile_header_t));
    strings = profile + sizeof(protecc_rule_profile_header_t) + rules_size;
    classes = (const protecc_profile_charclass_entry_t*)(profile + header->charclass_table_off);

    if (!__VALID_PROFILE_PTR(profile, rules, rules_size)) {
        return false;
    }

    if (!__VALID_PROFILE_PTR(profile, strings, header->strings_size)) {
        return false;
    }

    if (class_table_size > 0 && !__VALID_PROFILE_PTR(profile, classes, class_table_size)) {
        return false;
    }

    if (header->rule_count == 0 || header->dfa_section_off == 0) {
        return false;
    }

    dfa = (const protecc_profile_dfa_t*)(profile + header->dfa_section_off);
    if (!__VALID_PROFILE_PTR(profile, dfa, sizeof(*dfa))) {
        return false;
    }

    dfa_base = (const __u8*)dfa;

    if (dfa->num_states == 0 || dfa->num_classes == 0 || dfa->num_classes > PROTECC_PROFILE_DFA_CLASSMAP_SIZE) {
        return false;
    }

    classmap = profile + header->dfa_section_off + dfa->classmap_off;
    accept = (const __u32*)(profile + header->dfa_section_off + dfa->accept_off);
    candidate_index = (const __u32*)(profile + header->dfa_section_off + dfa->candidate_index_off);
    candidate_count = (const __u32*)(profile + header->dfa_section_off + dfa->candidate_count_off);
    candidates = (const __u32*)(profile + header->dfa_section_off + dfa->candidates_off);
    transitions = (const __u32*)(profile + header->dfa_section_off + dfa->transitions_off);

    dfa_end = (__u64)header->dfa_section_off + dfa->transitions_off
        + ((__u64)dfa->num_states * (__u64)dfa->num_classes * sizeof(__u32));

    if (dfa_end > PROTECC_BPF_MAX_PROFILE_SIZE) {
        return false;
    }

    if (!__VALID_PROFILE_PTR(profile, classmap, PROTECC_PROFILE_DFA_CLASSMAP_SIZE)) {
        return false;
    }

    if (!__VALID_PROFILE_PTR(profile, accept, (size_t)dfa->accept_words * sizeof(__u32))) {
        return false;
    }

    if (!__VALID_PROFILE_PTR(profile, candidate_index, (size_t)dfa->num_states * sizeof(__u32))) {
        return false;
    }

    if (!__VALID_PROFILE_PTR(profile, candidate_count, (size_t)dfa->num_states * sizeof(__u32))) {
        return false;
    }

    if (!__VALID_PROFILE_PTR(profile, candidates, (size_t)dfa->candidates_count * sizeof(__u32))) {
        return false;
    }

    if (!__VALID_PROFILE_PTR(profile, transitions, (size_t)dfa->num_states * (size_t)dfa->num_classes * sizeof(__u32))) {
        return false;
    }

    {
        __u32 state = dfa->start_state;
        __u64 combined_len = (__u64)request->source.len + 1u + (__u64)request->target.len;

        if (combined_len > PROTECC_MAX_GLOB_STEPS) {
            return false;
        }

        for (__u64 idx = 0; idx < combined_len; idx++) {
            __u8 c;
            __u32 cls;
            __u64 index;

            if (idx < request->source.len) {
                c = request->source.data[idx];
            } else if (idx == request->source.len) {
                c = (uint8_t)PROTECC_MOUNT_DFA_SEPARATOR;
            } else {
                c = request->target.data[idx - request->source.len - 1u];
            }

            cls = classmap[c];
            if (cls >= dfa->num_classes) {
                return false;
            }

            index = ((__u64)state * (__u64)dfa->num_classes) + (__u64)cls;
            state = transitions[index];
            if (state >= dfa->num_states) {
                return false;
            }
        }

        if ((accept[state >> 5] & (1u << (state & 31u))) == 0u) {
            return false;
        }

        if ((__u64)candidate_index[state] + (__u64)candidate_count[state] > dfa->candidates_count) {
            return false;
        }

        case_insensitive = (header->flags & PROTECC_PROFILE_FLAG_CASE_INSENSITIVE) != 0;

        bpf_for (i, 0, PROTECC_BPF_MAX_MOUNT_RULES) {
            __u32 rule_index;
            const protecc_mount_profile_rule_t* rule;

            if (i >= candidate_count[state]) {
                break;
            }

            rule_index = candidates[candidate_index[state] + i];
            if (rule_index >= header->rule_count) {
                return false;
            }

            rule = &rules[rule_index];

            if (rule->flags != 0 && (request->flags & rule->flags) != rule->flags) {
                continue;
            }

            if (!__protecc_bpf_profile_match(profile, strings,
                                              header->strings_size,
                                              classes,
                                              header->charclass_count,
                                              rule->fstype_pattern_off,
                                              &request->fstype,
                                              case_insensitive)) {
                continue;
            }

            if (!__protecc_bpf_profile_match(profile, strings,
                                              header->strings_size,
                                              classes,
                                              header->charclass_count,
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
    }

    return false;
}

#endif // !__PROTECC_BPF_MOUNT_H__
