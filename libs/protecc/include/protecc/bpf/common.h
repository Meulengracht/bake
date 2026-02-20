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

#ifndef __PROTECC_BPF_COMMON_H__
#define __PROTECC_BPF_COMMON_H__

#include <protecc/bpf/path.h>

#ifndef PROTECC_BPF_MAX_NET_RULES
#define PROTECC_BPF_MAX_NET_RULES 256u
#endif

#ifndef PROTECC_BPF_MAX_MOUNT_RULES
#define PROTECC_BPF_MAX_MOUNT_RULES 256u
#endif

#ifndef PROTECC_BPF_MAX_GLOB_STEPS
#define PROTECC_BPF_MAX_GLOB_STEPS 1024u
#endif

#ifndef PROTECC_BPF_MAX_CHARCLASS_SPAN
#define PROTECC_BPF_MAX_CHARCLASS_SPAN 64u
#endif

typedef struct {
    const __u8* data;
    __u32       len;
} protecc_bpf_string_t;

static __always_inline __u8 __protecc_bpf_to_lower(__u8 ch)
{
    if (ch >= 'A' && ch <= 'Z') {
        return (__u8)(ch + ('a' - 'A'));
    }
    return ch;
}

static __always_inline bool __protecc_bpf_chars_equal(__u8 a, __u8 b, bool case_insensitive)
{
    if (case_insensitive) {
        a = __protecc_bpf_to_lower(a);
        b = __protecc_bpf_to_lower(b);
    }
    return a == b;
}

static __always_inline bool __protecc_bpf_pattern_char(
    const __u8* strings,
    __u32       strings_size,
    __u32       pattern_off,
    __u32       index,
    __u8*       ch_out)
{
    __u32 absolute;

    if (!strings || !ch_out) {
        return false;
    }

    if (pattern_off >= strings_size) {
        return false;
    }

    absolute = pattern_off + index;
    if (absolute < pattern_off || absolute >= strings_size) {
        return false;
    }

    *ch_out = strings[absolute];
    return true;
}

static __always_inline bool __protecc_bpf_charclass_match(
    const __u8* strings,
    __u32       strings_size,
    __u32       pattern_off,
    __u8        value,
    bool        case_insensitive,
    __u32*      consumed_out)
{
    __u32 index = 1;
    bool invert = false;
    bool matched = false;
    __u8 token;

    if (!consumed_out) {
        return false;
    }

    if (!__protecc_bpf_pattern_char(strings, strings_size, pattern_off, 0, &token)) {
        return false;
    }

    if (token != '[') {
        *consumed_out = 1;
        return __protecc_bpf_chars_equal(token, value, case_insensitive);
    }

    if (!__protecc_bpf_pattern_char(strings, strings_size, pattern_off, index, &token)) {
        return false;
    }

    if (token == '!' || token == '^') {
        invert = true;
        index++;
        if (!__protecc_bpf_pattern_char(strings, strings_size, pattern_off, index, &token)) {
            return false;
        }
    }

    if (token == ']') {
        if (__protecc_bpf_chars_equal(token, value, case_insensitive)) {
            matched = true;
        }
        index++;
    }

    bpf_for (__u32 scan, 0, PROTECC_BPF_MAX_CHARCLASS_SPAN) {
        __u8 first;
        __u8 dash;
        __u8 last;

        if (!__protecc_bpf_pattern_char(strings, strings_size, pattern_off, index, &first)) {
            *consumed_out = 1;
            return __protecc_bpf_chars_equal('[', value, case_insensitive);
        }

        if (first == '\0') {
            *consumed_out = 1;
            return __protecc_bpf_chars_equal('[', value, case_insensitive);
        }

        if (first == ']') {
            *consumed_out = index + 1u;
            return invert ? !matched : matched;
        }

        if (__protecc_bpf_pattern_char(strings, strings_size, pattern_off, index + 1u, &dash)
            && dash == '-'
            && __protecc_bpf_pattern_char(strings, strings_size, pattern_off, index + 2u, &last)
            && last != '\0'
            && last != ']') {
            __u8 left = first;
            __u8 right = last;
            __u8 cmp = value;

            if (case_insensitive) {
                left = __protecc_bpf_to_lower(left);
                right = __protecc_bpf_to_lower(right);
                cmp = __protecc_bpf_to_lower(cmp);
            }

            if (left <= cmp && cmp <= right) {
                matched = true;
            }

            index += 3u;
            continue;
        }

        if (__protecc_bpf_chars_equal(first, value, case_insensitive)) {
            matched = true;
        }
        index++;
    }

    *consumed_out = 1;
    return __protecc_bpf_chars_equal('[', value, case_insensitive);
}

static __always_inline bool __protecc_bpf_glob_match(
    const __u8*                 strings,
    __u32                       strings_size,
    __u32                       pattern_off,
    const protecc_bpf_string_t* value,
    bool                        case_insensitive)
{
    __u32 pattern_index = 0;
    __u32 value_index = 0;
    __u32 star_pattern = 0xFFFFFFFFu;
    __u32 star_value = 0xFFFFFFFFu;

    if (!strings || !value || (value->len != 0 && value->data == NULL)) {
        return false;
    }

    bpf_for (__u32 steps, 0, PROTECC_BPF_MAX_GLOB_STEPS) {
        __u8 pc;

        if (!__protecc_bpf_pattern_char(strings, strings_size, pattern_off, pattern_index, &pc)) {
            return false;
        }

        if (value_index >= value->len) {
            if (pc == '*') {
                pattern_index++;
                continue;
            }
            return pc == '\0';
        }

        if (pc == '*') {
            pattern_index++;
            star_pattern = pattern_index;
            star_value = value_index;
            continue;
        }

        if (pc == '?') {
            pattern_index++;
            value_index++;
            continue;
        }

        if (pc == '[') {
            __u32 consumed = 0;
            if (__protecc_bpf_charclass_match(strings,
                                              strings_size,
                                              pattern_off + pattern_index,
                                              value->data[value_index],
                                              case_insensitive,
                                              &consumed)) {
                pattern_index += consumed;
                value_index++;
                continue;
            }
        } else if (__protecc_bpf_chars_equal(pc, value->data[value_index], case_insensitive)) {
            pattern_index++;
            value_index++;
            continue;
        }

        if (star_pattern != 0xFFFFFFFFu) {
            pattern_index = star_pattern;
            star_value++;
            value_index = star_value;
            continue;
        }

        return false;
    }

    return false;
}

static __always_inline bool __protecc_bpf_match_optional(
    const __u8*                 strings,
    __u32                       strings_size,
    __u32                       pattern_off,
    const protecc_bpf_string_t* value,
    bool                        case_insensitive)
{
    if (pattern_off == PROTECC_PROFILE_STRING_NONE) {
        return true;
    }

    if (!value || (value->len != 0 && value->data == NULL)) {
        return false;
    }

    return __protecc_bpf_glob_match(strings, strings_size, pattern_off, value, case_insensitive);
}

#endif // !__PROTECC_BPF_COMMON_H__
