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

#ifndef __PROTECC_BPF_MATCH_H__
#define __PROTECC_BPF_MATCH_H__

#include <protecc/profile.h>
#include <bpf/bpf_helpers.h>

/**
 * @brief Tunables for pattern matching in BPF programs.
 */
#define PROTECC_BPF_MAX_NET_RULES PROTECC_MAX_NET_RULES
#define PROTECC_BPF_MAX_MOUNT_RULES 256u
#define PROTECC_BPF_MAX_GLOB_STEPS PROTECC_MAX_GLOB_STEPS
#define PROTECC_BPF_MAX_CHAR_CLASSES 256u

typedef struct {
    const __u8* data;
    __u32       len;
} protecc_bpf_string_t;

static __always_inline __u8 __protecc_bpf_to_lower(__u8 c)
{
    if (c >= 'A' && c <= 'Z') {
        return (__u8)(c + ('a' - 'A'));
    }
    return c;
}

static __always_inline bool __protecc_bpf_chars_equal(__u8 a, __u8 b, bool caseInsensitive)
{
    if (caseInsensitive) {
        a = __protecc_bpf_to_lower(a);
        b = __protecc_bpf_to_lower(b);
    }
    return a == b;
}

static __always_inline bool __protecc_bpf_pattern_char(
    const __u8* strings,
    __u32       stringsSize,
    __u32       patternOffset,
    __u32       index,
    __u8*       charOut)
{
    __u32 absolute;

    if (patternOffset >= stringsSize) {
        return false;
    }

    absolute = patternOffset + index;
    if (absolute < patternOffset) {
        return false;
    }

    if (__VALID_PTR(strings, stringsSize, strings + absolute, 1)) {
        *charOut = strings[absolute];
        return true;
    }
    return false;
}

static __always_inline const protecc_profile_charclass_entry_t* __protecc_bpf_find_charclass(
    const protecc_profile_charclass_entry_t* classes,
    __u32                                    classCount,
    __u32                                    patternOffset)
{
    __u32 i;

    bpf_for (i, 0, PROTECC_BPF_MAX_CHAR_CLASSES) {
        if (i >= classCount) {
            break;
        }

        if (classes[i].pattern_off == patternOffset) {
            return &classes[i];
        }
    }
    return NULL;
}

static __always_inline bool __protecc_bpf_charclass_match_bitmap(
    const protecc_profile_charclass_entry_t* entry,
    __u8                                     value,
    bool                                     case_insensitive,
    __u32*                                   consumed_out)
{
    __u8 lowered;
    __u8 mask;
    __u8 byte_index;

    if (entry == NULL || entry->consumed == 0) {
        return false;
    }

    lowered = value;
    if (case_insensitive) {
        lowered = __protecc_bpf_to_lower(lowered);
    }

    byte_index = lowered >> 3u;
    mask = (uint8_t)(1u << (lowered & 7u));

    if (consumed_out) {
        *consumed_out = entry->consumed;
    }

    return (entry->bitmap[byte_index] & mask) != 0;
}

static __always_inline bool __protecc_bpf_glob_match(
    const __u8                               profile[PROTECC_BPF_MAX_PROFILE_SIZE],
    const __u8*                              strings,
    __u32                                    stringsSize,
    const protecc_profile_charclass_entry_t* classes,
    __u32                                    classCount,
    __u32                                    patternOffset,
    const protecc_bpf_string_t*              value,
    bool                                     caseInsensitive)
{
    __u32 pattern_index = 0;
    __u32 value_index = 0;
    __u32 star_pattern = 0xFFFFFFFFu;
    __u32 star_value = 0xFFFFFFFFu;
    __u32 steps;

    if (value == NULL || (value->len != 0 && value->data == NULL)) {
        return false;
    }

    bpf_for (steps, 0, PROTECC_BPF_MAX_GLOB_STEPS) {
        __u8 pc;

        if (!__protecc_bpf_pattern_char(strings, stringsSize, patternOffset, pattern_index, &pc)) {
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
            const protecc_profile_charclass_entry_t* ce;

            ce = __protecc_bpf_find_charclass(classes, classCount, patternOffset + pattern_index);
            if (__protecc_bpf_charclass_match_bitmap(ce,
                                                     value->data[value_index],
                                                     caseInsensitive,
                                                     &consumed)) {
                pattern_index += consumed;
                value_index++;
                continue;
            }
        } else if (__protecc_bpf_chars_equal(pc, value->data[value_index], caseInsensitive)) {
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

static __always_inline bool __protecc_bpf_profile_match(
    const __u8                               profile[PROTECC_BPF_MAX_PROFILE_SIZE],
    const __u8*                              strings,
    __u32                                    stringsSize,
    const protecc_profile_charclass_entry_t* classes,
    __u32                                    classCount,
    __u32                                    patternOffset,
    const protecc_bpf_string_t*              value,
    bool                                     caseInsensitive)
{
    if (patternOffset == PROTECC_PROFILE_STRING_NONE) {
        return true;
    }
    return __protecc_bpf_glob_match(
        profile,
        strings,
        stringsSize,
        classes,
        classCount,
        patternOffset,
        value,
        caseInsensitive
    );
}

#endif // !__PROTECC_BPF_MATCH_H__
