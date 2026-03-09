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

#include <protecc/profile.h>
#include <bpf/bpf_helpers.h>

#define PROTECC_BPF_MAX_NET_RULES PROTECC_MAX_RULES
#define PROTECC_BPF_MAX_NET_LENGTH 512
#define PROTECC_BPF_MAX_CHAR_CLASSES PROTECC_PROFILE_MAX_CHAR_CLASSES

#define PROTECC_BPF_NET_PROTOCOL_ANY 0u
#define PROTECC_BPF_NET_FAMILY_ANY   0u

typedef struct {
    const __u8* data;
    __u32       len;
} protecc_bpf_string_t;

typedef struct {
    __u8                 protocol;
    __u8                 family;
    __u16                port;
    protecc_bpf_string_t ip;
    protecc_bpf_string_t unix_path;
} protecc_bpf_net_request_t;

static __always_inline bool protecc_bpf_match_net(
    const __u8                       profile[PROTECC_BPF_MAX_PROFILE_SIZE],
    const protecc_bpf_net_request_t* request,
    __u8*                            actionOut)
{
    const protecc_rule_profile_header_t* header;
    const protecc_net_profile_rule_t*   rules;
    const protecc_net_dfa_section_t*    section;
    const protecc_profile_dfa_t*            dfa = NULL;
    __u64                               rulesSize;
    __u64                               classTableSize;
    __u64                               dfaSectionOff;
    __u64                               dfaBlockOff;
    __u64                               transitionsCount;
    __u64                               blockSize;
    __u32                               i;
    __u32                               state = 0;

    header = (const protecc_rule_profile_header_t*)profile;
    if (header->magic != PROTECC_NET_PROFILE_MAGIC || header->version != PROTECC_NET_PROFILE_VERSION) {
        return false;
    }

    if ((header->flags & ~(PROTECC_PROFILE_FLAG_CASE_INSENSITIVE)) != 0) {
        return false;
    }

    if (header->rule_count > PROTECC_BPF_MAX_NET_RULES) {
        return false;
    }

    if (header->charclass_count > PROTECC_BPF_MAX_CHAR_CLASSES) {
        return false;
    }

    rulesSize = (__u64)header->rule_count * sizeof(protecc_net_profile_rule_t);
    classTableSize = (__u64)header->charclass_count * sizeof(protecc_profile_charclass_entry_t);
    dfaSectionOff = header->dfa_section_off;

    if (dfaSectionOff == 0 || header->rule_count == 0) {
        return false;
    }

    if (header->charclass_table_off < sizeof(protecc_rule_profile_header_t) + rulesSize + (__u64)header->strings_size) {
        return false;
    }

    if (dfaSectionOff < header->charclass_table_off + classTableSize) {
        return false;
    }

    rules = (const protecc_net_profile_rule_t*)(profile + sizeof(protecc_rule_profile_header_t));
    if (!__VALID_PROFILE_PTR(profile, rules, rulesSize)) {
        return false;
    }

    if (dfaSectionOff > PROTECC_BPF_MAX_PROFILE_SIZE - sizeof(protecc_net_dfa_section_t)) {
        return false;
    }

    section = (const protecc_net_dfa_section_t*)(profile + dfaSectionOff);

    if (request->protocol == PROTECC_NET_PROTOCOL_UNIX || request->family == PROTECC_NET_FAMILY_UNIX) {
        if (section->unix_dfa_off == 0) {
            return false;
        }
        dfaBlockOff = dfaSectionOff + section->unix_dfa_off;
    } else {
        if (section->ip_dfa_off == 0) {
            return false;
        }
        dfaBlockOff = dfaSectionOff + section->ip_dfa_off;
    }

    if (dfaBlockOff > PROTECC_BPF_MAX_PROFILE_SIZE - sizeof(protecc_profile_dfa_t)) {
        return false;
    }

    dfa = (const protecc_profile_dfa_t*)(profile + dfaBlockOff);

    if (dfa->num_states == 0 || dfa->num_classes == 0 || dfa->num_classes > PROTECC_PROFILE_DFA_CLASSMAP_SIZE) {
        return false;
    }

    if (dfa->accept_words != ((dfa->num_states + 31u) / 32u)) {
        return false;
    }

    transitionsCount = (__u64)dfa->num_states * (__u64)dfa->num_classes;
    if (dfa->classmap_off < sizeof(protecc_profile_dfa_t)) {
        return false;
    }

    if (dfa->accept_off < dfa->classmap_off + PROTECC_PROFILE_DFA_CLASSMAP_SIZE) {
        return false;
    }

    if (dfa->candidate_index_off < dfa->accept_off + ((__u64)dfa->accept_words * sizeof(__u32))) {
        return false;
    }

    if (dfa->candidate_count_off < dfa->candidate_index_off + ((__u64)dfa->num_states * sizeof(__u32))) {
        return false;
    }

    if (dfa->candidates_off < dfa->candidate_count_off + ((__u64)dfa->num_states * sizeof(__u32))) {
        return false;
    }

    if (dfa->transitions_off < dfa->candidates_off + ((__u64)dfa->candidates_count * sizeof(__u32))) {
        return false;
    }

    blockSize = (__u64)dfa->transitions_off + (transitionsCount * sizeof(__u32));

    if (blockSize > PROTECC_BPF_MAX_PROFILE_SIZE || dfaBlockOff > PROTECC_BPF_MAX_PROFILE_SIZE - blockSize) {
        return false;
    }

    {
        const __u32* counts = (const __u32*)((const __u8*)dfa + dfa->candidate_count_off);
        const __u32* starts = (const __u32*)((const __u8*)dfa + dfa->candidate_index_off);
        __u32       total = 0;

        bpf_for (i, 0, PROTECC_BPF_MAX_NET_RULES) {
            if (i >= dfa->num_states) {
                break;
            }

            if (counts[i] > header->rule_count) {
                return false;
            }

            if ((__u64)starts[i] + (__u64)counts[i] > dfa->candidates_count) {
                return false;
            }

            total += counts[i];
        }

        if (total != dfa->candidates_count) {
            return false;
        }
    }

    {
        const __u8* classmap = ((const __u8*)dfa) + dfa->classmap_off;
        const __u32* transitions = (const __u32*)(((const __u8*)dfa) + dfa->transitions_off);
        const protecc_bpf_string_t* value;
        __u32 len_limit;

        value = (request->protocol == PROTECC_NET_PROTOCOL_UNIX || request->family == PROTECC_NET_FAMILY_UNIX)
            ? &request->unix_path
            : &request->ip;

        if (value && value->len > PROTECC_BPF_MAX_NET_LENGTH) {
            return false;
        }

        len_limit = value ? value->len : 0;
        bpf_for (i, 0, PROTECC_BPF_MAX_NET_LENGTH) {
            __u8 cls;
            __u64 idx;

            if (i >= len_limit) {
                break;
            }

            if (value == NULL || value->data == NULL) {
                return false;
            }

            cls = classmap[value->data[i]];
            if (cls >= dfa->num_classes) {
                return false;
            }

            idx = ((__u64)state * (__u64)dfa->num_classes) + (__u64)cls;
            state = transitions[idx];
            if (state >= dfa->num_states) {
                return false;
            }
        }

        if (len_limit > PROTECC_BPF_MAX_NET_LENGTH) {
            return false;
        }
    }

    {
        const __u32* accept = (const __u32*)((const __u8*)dfa + dfa->accept_off);
        const __u32* cand_index = (const __u32*)((const __u8*)dfa + dfa->candidate_index_off);
        const __u32* cand_count = (const __u32*)((const __u8*)dfa + dfa->candidate_count_off);
        const __u32* candidates = (const __u32*)((const __u8*)dfa + dfa->candidates_off);
        __u32        start;
        __u32        count;

        if ((accept[state >> 5] & (1u << (state & 31u))) == 0u) {
            return false;
        }

        start = cand_index[state];
        count = cand_count[state];

        if ((__u64)start + (__u64)count > dfa->candidates_count) {
            return false;
        }

        bpf_for (i, 0, PROTECC_BPF_MAX_NET_RULES) {
            const protecc_net_profile_rule_t* rule;

            if (i >= count) {
                break;
            }

            if (start + i >= dfa->candidates_count) {
                return false;
            }

            if (candidates[start + i] >= header->rule_count) {
                return false;
            }

            rule = &rules[candidates[start + i]];

            if (rule->protocol != PROTECC_BPF_NET_PROTOCOL_ANY && rule->protocol != request->protocol) {
                continue;
            }

            if (rule->family != PROTECC_BPF_NET_FAMILY_ANY && rule->family != request->family) {
                continue;
            }

            if (request->port < rule->port_from || request->port > rule->port_to) {
                continue;
            }

            *actionOut = rule->action;
            return true;
        }
    }

    return false;
}

#endif // !__PROTECC_BPF_NET_H__
