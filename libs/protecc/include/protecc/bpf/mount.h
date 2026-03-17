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

#include <protecc/profile.h>
#include <bpf/bpf_helpers.h>

typedef struct {
    const __u8* data;
    __u32       len;
} protecc_bpf_string_t;

typedef struct {
    __u32                 flags;
    protecc_bpf_string_t  source;
    protecc_bpf_string_t  target;
    protecc_bpf_string_t  fstype;
    protecc_bpf_string_t  options;
} protecc_bpf_mount_request_t;

static __always_inline bool __protecc_bpf_mount_read_u32(
    const __u8 profile[PROTECC_BPF_MAX_PROFILE_SIZE],
    __u64      base_off,
    __u32      table_off,
    __u32      index,
    __u32*     value_out);

static __always_inline bool __protecc_bpf_mount_validate_candidate_tables(
    const __u8                   profile[PROTECC_BPF_MAX_PROFILE_SIZE],
    const protecc_profile_dfa_t* dfa,
    __u64                        dfa_block_off,
    __u32                        rule_count)
{
    __u32 i;
    __u32 total = 0;

    bpf_for (i, 0, PROTECC_MAX_RULES) {
        __u32 count;
        __u32 start;

        if (i >= dfa->num_states) {
            break;
        }

        if (!__protecc_bpf_mount_read_u32(
                profile,
                dfa_block_off,
                dfa->candidate_count_off,
                i,
                &count)) {
            return false;
        }

        if (count > rule_count) {
            return false;
        }

        if (!__protecc_bpf_mount_read_u32(
                profile,
                dfa_block_off,
                dfa->candidate_index_off,
                i,
                &start)) {
            return false;
        }

        if ((__u64)start > dfa->candidates_count) {
            return false;
        }

        if ((__u64)count > (dfa->candidates_count - (__u64)start)) {
            return false;
        }

        total += count;
    }

    return total == dfa->candidates_count;
}

static __always_inline bool __protecc_bpf_mount_validate_dfa_layout(
    const protecc_profile_dfa_t* dfa,
    __u64                        dfa_block_off)
{
    __u64 transitions_count;
    __u64 dfa_block_size;

    if (dfa->num_states == 0 || dfa->num_classes == 0 || dfa->num_classes > PROTECC_PROFILE_DFA_CLASSMAP_SIZE) {
        return false;
    }

    if (dfa->accept_words != ((dfa->num_states + 31u) / 32u)) {
        return false;
    }

    transitions_count = (__u64)dfa->num_states * (__u64)dfa->num_classes;

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

    dfa_block_size = (__u64)dfa->transitions_off + (transitions_count * sizeof(__u32));
    if (dfa_block_size > PROTECC_BPF_MAX_PROFILE_SIZE || dfa_block_off > PROTECC_BPF_MAX_PROFILE_SIZE - dfa_block_size) {
        return false;
    }

    return true;
}

static __always_inline bool __protecc_bpf_mount_read_u32(
    const __u8 profile[PROTECC_BPF_MAX_PROFILE_SIZE],
    __u64      base_off,
    __u32      table_off,
    __u32      index,
    __u32*     value_out)
{
    const __u32* value_ptr;
    __u64        value_off;

    if (value_out == NULL) {
        return false;
    }

    value_off = base_off + (__u64)table_off + ((__u64)index * sizeof(__u32));
    if (value_off > PROTECC_BPF_MAX_PROFILE_SIZE - sizeof(__u32)) {
        return false;
    }

    value_ptr = (const __u32*)(profile + value_off);
    if (!__VALID_PROFILE_PTR(profile, value_ptr, sizeof(__u32))) {
        return false;
    }

    *value_out = *value_ptr;
    return true;
}

static __always_inline bool __protecc_bpf_mount_read_accept_bits(
    const __u8                   profile[PROTECC_BPF_MAX_PROFILE_SIZE],
    const protecc_profile_dfa_t* dfa,
    __u64                        dfa_block_off,
    __u32                        state,
    __u32*                       accept_bits_out)
{
    __u32 accept_word;

    accept_word = state >> 5;
    if (accept_word >= dfa->accept_words) {
        return false;
    }

    return __protecc_bpf_mount_read_u32(
        profile,
        dfa_block_off,
        dfa->accept_off,
        accept_word,
        accept_bits_out
    );
}

static __always_inline bool __protecc_bpf_mount_read_state_candidates(
    const __u8                   profile[PROTECC_BPF_MAX_PROFILE_SIZE],
    const protecc_profile_dfa_t* dfa,
    __u64                        dfa_block_off,
    __u32                        state,
    __u32*                       state_candidate_index_out,
    __u32*                       state_candidate_count_out)
{
    __u32 state_candidate_index;
    __u32 state_candidate_count;

    if (!__protecc_bpf_mount_read_u32(
            profile,
            dfa_block_off,
            dfa->candidate_index_off,
            state,
            &state_candidate_index)) {
        return false;
    }

    if (!__protecc_bpf_mount_read_u32(
            profile,
            dfa_block_off,
            dfa->candidate_count_off,
            state,
            &state_candidate_count)) {
        return false;
    }

    if ((__u64)state_candidate_index > dfa->candidates_count) {
        return false;
    }

    if ((__u64)state_candidate_count > (dfa->candidates_count - (__u64)state_candidate_index)) {
        return false;
    }

    if (state_candidate_index_out) {
        *state_candidate_index_out = state_candidate_index;
    }

    if (state_candidate_count_out) {
        *state_candidate_count_out = state_candidate_count;
    }

    return true;
}

static __always_inline bool __protecc_bpf_mount_dfa_step(
    const __u8                   profile[PROTECC_BPF_MAX_PROFILE_SIZE],
    const protecc_profile_dfa_t* dfa,
    __u64                        dfa_block_off,
    __u32                        num_states,
    __u32                        num_classes,
    __u32                        transitions_count32,
    __u32*                       state_inout,
    __u8                         c)
{
    const __u8* class_ptr;
    const __u32* state_ptr;
    __u32 state;
    __u32 cls;
    __u32 index32;
    __u64 class_off;
    __u64 state_off;

    state = *state_inout;
    if (state >= num_states) {
        return false;
    }

    class_off = dfa_block_off + dfa->classmap_off + (__u64)c;
    if (class_off > PROTECC_BPF_MAX_PROFILE_SIZE - sizeof(__u8)) {
        return false;
    }

    class_ptr = (const __u8*)(profile + class_off);
    if (!__VALID_PROFILE_PTR(profile, class_ptr, sizeof(__u8))) {
        return false;
    }

    cls = *class_ptr;
    if (cls >= num_classes) {
        return false;
    }

    index32 = (state * num_classes) + cls;
    if (index32 >= transitions_count32) {
        return false;
    }

    state_off = dfa_block_off + dfa->transitions_off + ((__u64)index32 * sizeof(__u32));
    if (state_off > PROTECC_BPF_MAX_PROFILE_SIZE - sizeof(__u32)) {
        return false;
    }

    state_ptr = (const __u32*)(profile + state_off);
    if (!__VALID_PROFILE_PTR(profile, state_ptr, sizeof(__u32))) {
        return false;
    }

    state = *state_ptr;
    if (state >= num_states) {
        return false;
    }

    *state_inout = state;
    return true;
}

static __always_inline bool __protecc_bpf_mount_run_dfa(
    const __u8                         profile[PROTECC_BPF_MAX_PROFILE_SIZE],
    const protecc_profile_dfa_t*       dfa,
    __u64                              dfa_block_off,
    const protecc_bpf_mount_request_t* request,
    __u32*                             state_out)
{
    __u32 state;
    __u32 accept_bits;
    __u32 transitions_count;
    __u32 source_target_len;
    __u32 idx;
    __u32 combined_len;

    state = dfa->start_state;
    combined_len = request->source.len + 1u + request->target.len;

    if (combined_len > PROTECC_MAX_GLOB_STEPS) {
        return false;
    }

    if ((request->source.len > 0 && request->source.data == NULL) ||
        (request->target.len > 0 && request->target.data == NULL)) {
        return false;
    }

    source_target_len = request->source.len + request->target.len;
    if (source_target_len < request->source.len || source_target_len > (PROTECC_MAX_GLOB_STEPS - 1u)) {
        return false;
    }

    transitions_count = dfa->num_states * dfa->num_classes;
    bpf_for (idx, 0, PROTECC_MAX_GLOB_STEPS) {
        __u8 c;

        if (idx >= request->source.len) {
            break;
        }

        if (!__VALID_PTR(request->source.data, request->source.len, &request->source.data[idx], 1)) {
            return false;
        }
        c = request->source.data[idx];

        if (!__protecc_bpf_mount_dfa_step(
                profile,
                dfa,
                dfa_block_off,
                dfa->num_states,
                dfa->num_classes,
                transitions_count,
                &state,
                c)) {
            return false;
        }
    }

    if (!__protecc_bpf_mount_dfa_step(
            profile,
            dfa,
            dfa_block_off,
            dfa->num_states,
            dfa->num_classes,
            transitions_count,
            &state,
            (__u8)PROTECC_MOUNT_DFA_SEPARATOR)) {
        return false;
    }

    bpf_for (idx, 0, PROTECC_MAX_GLOB_STEPS) {
        __u8 c;

        if (idx >= request->target.len) {
            break;
        }

        if (!__VALID_PTR(request->target.data, request->target.len, &request->target.data[idx], 1)) {
            return false;
        }
        c = request->target.data[idx];

        if (!__protecc_bpf_mount_dfa_step(
                profile,
                dfa,
                dfa_block_off,
                dfa->num_states,
                dfa->num_classes,
                transitions_count,
                &state,
                c)) {
            return false;
        }
    }

    if (!__protecc_bpf_mount_read_accept_bits(
            profile,
            dfa,
            dfa_block_off,
            state,
            &accept_bits)) {
        return false;
    }

    if ((accept_bits & (1u << (state & 31u))) == 0u) {
        return false;
    }

    if (!__protecc_bpf_mount_read_state_candidates(
            profile,
            dfa,
            dfa_block_off,
            state,
            NULL,
            NULL)) {
        return false;
    }

    *state_out = state;
    return true;
}

static __always_inline bool __protecc_bpf_mount_run_string_dfa(
    const __u8                         profile[PROTECC_BPF_MAX_PROFILE_SIZE],
    const protecc_profile_dfa_t*       dfa,
    __u64                              dfa_block_off,
    const protecc_bpf_string_t*        value,
    __u32*                             state_out)
{
    __u32 state;
    __u32 accept_bits;
    __u32 num_states;
    __u32 num_classes;
    __u32 transitions_count32;
    __u32 idx;

    if (value == NULL || state_out == NULL) {
        return false;
    }

    if (value->len > PROTECC_MAX_GLOB_STEPS || (value->len > 0 && value->data == NULL)) {
        return false;
    }

    state = dfa->start_state;
    num_states = dfa->num_states;
    num_classes = dfa->num_classes;

    if (num_states == 0 || num_classes == 0 || num_classes > PROTECC_PROFILE_DFA_CLASSMAP_SIZE) {
        return false;
    }

    if ((__u64)num_states > ((__u64)PROTECC_BPF_MAX_PROFILE_SIZE / sizeof(__u32))) {
        return false;
    }

    if ((__u64)num_states * (__u64)num_classes > ((__u64)PROTECC_BPF_MAX_PROFILE_SIZE / sizeof(__u32))) {
        return false;
    }

    transitions_count32 = num_states * num_classes;

    bpf_for (idx, 0, PROTECC_MAX_GLOB_STEPS) {
        __u8 c;

        if (idx >= value->len) {
            break;
        }

        if (value->data == NULL || !__VALID_PTR(value->data, value->len, &value->data[idx], 1)) {
            return false;
        }
        c = value->data[idx];

        if (!__protecc_bpf_mount_dfa_step(
                profile,
                dfa,
                dfa_block_off,
                num_states,
                num_classes,
                transitions_count32,
                &state,
                c)) {
            return false;
        }
    }

    if (!__protecc_bpf_mount_read_accept_bits(
            profile,
            dfa,
            dfa_block_off,
            state,
            &accept_bits)) {
        return false;
    }

    if ((accept_bits & (1u << (state & 31u))) == 0u) {
        return false;
    }

    if (!__protecc_bpf_mount_read_state_candidates(
            profile,
            dfa,
            dfa_block_off,
            state,
            NULL,
            NULL)) {
        return false;
    }

    *state_out = state;
    return true;
}

static __always_inline bool __protecc_bpf_mount_candidate_contains(
    const __u8                   profile[PROTECC_BPF_MAX_PROFILE_SIZE],
    const protecc_profile_dfa_t* dfa,
    __u64                        dfa_block_off,
    __u32                        state,
    __u32                        rule_index)
{
    __u32 state_candidate_index;
    __u32 state_candidate_count;
    __u32 i;

    if (!__protecc_bpf_mount_read_state_candidates(
            profile,
            dfa,
            dfa_block_off,
            state,
            &state_candidate_index,
            &state_candidate_count)) {
        return false;
    }

    bpf_for (i, 0, PROTECC_MAX_RULES) {
        const __u32* candidate_ptr;
        __u32        candidate_rule_index;
        __u64        candidate_off;

        if (i >= state_candidate_count) {
            break;
        }

        if ((__u64)state_candidate_index + (__u64)i >= (__u64)dfa->candidates_count) {
            return false;
        }

        candidate_off = dfa_block_off + dfa->candidates_off + (((__u64)state_candidate_index + (__u64)i) * sizeof(__u32));
        if (candidate_off > PROTECC_BPF_MAX_PROFILE_SIZE - sizeof(__u32)) {
            return false;
        }

        candidate_ptr = (const __u32*)(profile + candidate_off);
        if (!__VALID_PROFILE_PTR(profile, candidate_ptr, sizeof(__u32))) {
            return false;
        }

        candidate_rule_index = *candidate_ptr;
        if (candidate_rule_index == rule_index) {
            return true;
        }
    }

    return false;
}

static __always_inline bool __protecc_bpf_mount_select_action(
    const __u8                        profile[PROTECC_BPF_MAX_PROFILE_SIZE],
    __u32                             rule_count,
    const protecc_mount_profile_rule_t*  rules,
    const protecc_profile_dfa_t*      source_target_dfa,
    __u64                             source_target_dfa_block_off,
    const protecc_profile_dfa_t*      fstype_dfa,
    __u64                             fstype_dfa_block_off,
    __u32                             fstype_state,
    bool                              has_fstype_state,
    const protecc_profile_dfa_t*      options_dfa,
    __u64                             options_dfa_block_off,
    __u32                             options_state,
    bool                              has_options_state,
    const protecc_bpf_mount_request_t* request,
    __u32                              state,
    __u8*                              action_out)
{
    __u32 state_candidate_index;
    __u32 state_candidate_count;
    __u32 i;

    if (rule_count == 0 || rule_count > PROTECC_MAX_RULES) {
        return false;
    }

    if (!__protecc_bpf_mount_read_state_candidates(
            profile,
            source_target_dfa,
            source_target_dfa_block_off,
            state,
            &state_candidate_index,
            &state_candidate_count)) {
        return false;
    }

    bpf_for (i, 0, PROTECC_MAX_RULES) {
        const protecc_mount_profile_rule_t* rule;
        const __u32*                        candidate_ptr;
        __u32                               rule_index;
        __u64                               candidate_off;

        if (i >= state_candidate_count) {
            break;
        }

        if ((__u64)state_candidate_index + (__u64)i >= (__u64)source_target_dfa->candidates_count) {
            return false;
        }

        candidate_off = source_target_dfa_block_off
            + source_target_dfa->candidates_off
            + (((__u64)state_candidate_index + (__u64)i) * sizeof(__u32));
        if (candidate_off > PROTECC_BPF_MAX_PROFILE_SIZE - sizeof(__u32)) {
            return false;
        }

        candidate_ptr = (const __u32*)(profile + candidate_off);
        if (!__VALID_PROFILE_PTR(profile, candidate_ptr, sizeof(__u32))) {
            return false;
        }

        rule_index = *candidate_ptr;
        if (rule_index >= rule_count) {
            return false;
        }

        rule = &rules[rule_index];

        if (rule->flags != 0 && (request->flags & rule->flags) != rule->flags) {
            continue;
        }

        if (rule->fstype_pattern_off != PROTECC_PROFILE_STRING_NONE) {
            if (!has_fstype_state) {
                continue;
            }

            if (!__protecc_bpf_mount_candidate_contains(
                    profile,
                    fstype_dfa,
                    fstype_dfa_block_off,
                    fstype_state,
                    rule_index)) {
                continue;
            }
        }

        if (rule->options_pattern_off != PROTECC_PROFILE_STRING_NONE) {
            if (!has_options_state) {
                continue;
            }

            if (!__protecc_bpf_mount_candidate_contains(
                    profile,
                    options_dfa,
                    options_dfa_block_off,
                    options_state,
                    rule_index)) {
                continue;
            }
        }

        if (action_out) {
            *action_out = rule->action;
        }
        return true;
    }

    return false;
}

static __always_inline bool protecc_bpf_match_mount(
    const __u8                         profile[PROTECC_BPF_MAX_PROFILE_SIZE],
    const protecc_bpf_mount_request_t* request,
    __u8*                              action_out)
{
    const protecc_rule_profile_header_t*      header;
    const protecc_mount_profile_rule_t*       rules;
    const protecc_mount_dfa_section_t*        dfa_section;
    const protecc_profile_dfa_t*              source_target_dfa;
    const protecc_profile_dfa_t*              fstype_dfa = NULL;
    const protecc_profile_dfa_t*              options_dfa = NULL;
    __u64                                     rules_size;
    __u64                                     required_size;
    __u64                                     dfa_section_off;
    __u64                                     source_target_dfa_block_off;
    __u64                                     fstype_dfa_block_off = 0;
    __u64                                     options_dfa_block_off = 0;
    __u32                                     state;
    __u32                                     fstype_state = 0;
    __u32                                     options_state = 0;
    bool                                      has_fstype_state = false;
    bool                                      has_options_state = false;

    if (!profile || !request) {
        return false;
    }

    header = (const protecc_rule_profile_header_t*)profile;
    if (header->magic != PROTECC_MOUNT_PROFILE_MAGIC || header->version != PROTECC_MOUNT_PROFILE_VERSION) {
        return false;
    }

    if (header->flags != 0) {
        return false;
    }

    if (header->rule_count > PROTECC_MAX_RULES) {
        return false;
    }

    if (header->charclass_count != 0 || header->charclass_table_off != 0) {
        return false;
    }

    rules_size = (__u64)header->rule_count * sizeof(protecc_mount_profile_rule_t);

    required_size = (__u64)sizeof(protecc_rule_profile_header_t) + rules_size + (__u64)header->strings_size;
    if (required_size < (__u64)sizeof(protecc_rule_profile_header_t) || required_size > PROTECC_BPF_MAX_PROFILE_SIZE) {
        return false;
    }

    rules = (const protecc_mount_profile_rule_t*)(profile + sizeof(protecc_rule_profile_header_t));

    if (!__VALID_PROFILE_PTR(profile, rules, rules_size)) {
        return false;
    }

    if (header->rule_count == 0 || header->dfa_section_off == 0) {
        return false;
    }

    dfa_section_off = header->dfa_section_off;
    dfa_section = (const protecc_mount_dfa_section_t*)(profile + dfa_section_off);
    if (!__VALID_PROFILE_PTR(profile, dfa_section, sizeof(*dfa_section))) {
        return false;
    }

    if (dfa_section->source_target_dfa_off == 0) {
        return false;
    }

    source_target_dfa_block_off = dfa_section_off + dfa_section->source_target_dfa_off;
    if (source_target_dfa_block_off < dfa_section_off || source_target_dfa_block_off > PROTECC_BPF_MAX_PROFILE_SIZE - sizeof(protecc_profile_dfa_t)) {
        return false;
    }

    source_target_dfa = (const protecc_profile_dfa_t*)(profile + source_target_dfa_block_off);
    if (!__VALID_PROFILE_PTR(profile, source_target_dfa, sizeof(*source_target_dfa))) {
        return false;
    }

    if (!__protecc_bpf_mount_validate_dfa_layout(source_target_dfa, source_target_dfa_block_off)) {
        return false;
    }

    if (!__protecc_bpf_mount_validate_candidate_tables(profile, source_target_dfa, source_target_dfa_block_off, header->rule_count)) {
        return false;
    }

    if (dfa_section->fstype_dfa_off != 0) {
        fstype_dfa_block_off = dfa_section_off + dfa_section->fstype_dfa_off;
        if (fstype_dfa_block_off < dfa_section_off || fstype_dfa_block_off > PROTECC_BPF_MAX_PROFILE_SIZE - sizeof(protecc_profile_dfa_t)) {
            return false;
        }

        fstype_dfa = (const protecc_profile_dfa_t*)(profile + fstype_dfa_block_off);
        if (!__VALID_PROFILE_PTR(profile, fstype_dfa, sizeof(*fstype_dfa))) {
            return false;
        }

        if (!__protecc_bpf_mount_validate_dfa_layout(fstype_dfa, fstype_dfa_block_off)) {
            return false;
        }

        if (!__protecc_bpf_mount_validate_candidate_tables(profile, fstype_dfa, fstype_dfa_block_off, header->rule_count)) {
            return false;
        }
    }

    if (dfa_section->options_dfa_off != 0) {
        options_dfa_block_off = dfa_section_off + dfa_section->options_dfa_off;
        if (options_dfa_block_off < dfa_section_off || options_dfa_block_off > PROTECC_BPF_MAX_PROFILE_SIZE - sizeof(protecc_profile_dfa_t)) {
            return false;
        }

        options_dfa = (const protecc_profile_dfa_t*)(profile + options_dfa_block_off);
        if (!__VALID_PROFILE_PTR(profile, options_dfa, sizeof(*options_dfa))) {
            return false;
        }

        if (!__protecc_bpf_mount_validate_dfa_layout(options_dfa, options_dfa_block_off)) {
            return false;
        }

        if (!__protecc_bpf_mount_validate_candidate_tables(profile, options_dfa, options_dfa_block_off, header->rule_count)) {
            return false;
        }
    }

    if (!__protecc_bpf_mount_run_dfa(
            profile,
            source_target_dfa,
            source_target_dfa_block_off,
            request,
            &state)) {
        return false;
    }

    if (fstype_dfa != NULL) {
        if (__protecc_bpf_mount_run_string_dfa(
                profile,
                fstype_dfa,
                fstype_dfa_block_off,
                &request->fstype,
                &fstype_state)) {
            has_fstype_state = true;
        }
    }

    if (options_dfa != NULL) {
        if (__protecc_bpf_mount_run_string_dfa(
                profile,
                options_dfa,
                options_dfa_block_off,
                &request->options,
                &options_state)) {
            has_options_state = true;
        }
    }

    if (__protecc_bpf_mount_select_action(
            profile,
            header->rule_count,
            rules,
            source_target_dfa,
            source_target_dfa_block_off,
            fstype_dfa,
            fstype_dfa_block_off,
            fstype_state,
            has_fstype_state,
            options_dfa,
            options_dfa_block_off,
            options_state,
            has_options_state,
            request,
            state,
            action_out)) {
        return true;
    }

    return false;
}

#endif // !__PROTECC_BPF_MOUNT_H__
