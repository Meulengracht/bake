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

#ifndef __BPF_PATH_MATCH_H__
#define __BPF_PATH_MATCH_H__

#include <vmlinux.h>

#include "common.h"

/* Basename rules: limited patterns to avoid full path parsing in BPF */
#define BASENAME_RULE_MAX 8
#define BASENAME_MAX_STR  64

/**
 * @brief Max tokens in a basename pattern.
 * Example supported:
 *   nvme[0-9]+n[0-9]+p[0-9]+     -> LIT, DIGITS+, LIT, DIGITS+, LIT, DIGITS+
 */
#define BASENAME_TOKEN_MAX 6

enum basename_token_type {
    BASENAME_TOKEN_EMPTY      = 0,
    BASENAME_TOKEN_LITERAL    = 1,
    BASENAME_TOKEN_DIGIT1     = 2,
    BASENAME_TOKEN_DIGITSPLUS = 3,
};

struct basename_rule {
    __u32 allow_mask;
    __u8  token_count;
    __u8  tail_wildcard;   /* if set, the last literal token only needs to match as a prefix */
    __u8  _pad[2];
    __u8  token_type[BASENAME_TOKEN_MAX];
    __u8  token_len[BASENAME_TOKEN_MAX];
    char  token[BASENAME_TOKEN_MAX][BASENAME_MAX_STR];
};

struct basename_policy_value {
    struct basename_rule rules[BASENAME_RULE_MAX];
};

static __always_inline int __read_dentry_name(struct dentry* dentry, char out[BASENAME_MAX_STR], __u32* out_len)
{
    struct qstr d_name = {};
    const unsigned char* name_ptr = NULL;
    __u32 len = 0;

    if (!dentry || !out || !out_len) {
        return -EACCES;
    }

    CORE_READ_INTO(&d_name, dentry, d_name);
    CORE_READ_INTO(&name_ptr, &d_name, name);
    CORE_READ_INTO(&len, &d_name, len);

    if (!name_ptr) {
        return -EACCES;
    }

    if (len >= BASENAME_MAX_STR) {
        len = BASENAME_MAX_STR - 1;
    }
    if (len > 0) {
        bpf_core_read(out, len, name_ptr);
    }
    out[len] = 0;
    *out_len = len;
    return 0;
}

static __always_inline int __match_qmark_bounded(const char* pattern, const char* s, __u32 n)
{
    for (int i = 0; i < BASENAME_MAX_STR; i++) {
        if ((__u32)i >= n) {
            break;
        }
        char pc = pattern[i];
        if (pc != '?' && pc != s[i]) {
            return 1;
        }
    }
    return 0;
}

static __always_inline int __match_basename_rule(const struct basename_rule* rule, const char name[BASENAME_MAX_STR], __u32 name_len)
{
    if (!rule || rule->token_count == 0) {
        return 0;
    }

    if (rule->token_count > BASENAME_TOKEN_MAX) {
        return 0;
    }

    __u32 pos = 0;

#pragma unroll
    for (int t = 0; t < BASENAME_TOKEN_MAX; t++) {
        if ((__u32)t >= rule->token_count) {
            continue;
        }

        __u8 tt = rule->token_type[t];
        if (tt == BASENAME_TOKEN_LITERAL) {
            __u32 len = (__u32)rule->token_len[t];
            if (len >= BASENAME_MAX_STR) {
                return 0;
            }

            if (pos + len > name_len) {
                return 0;
            }

            // If this is the final token and tail_wildcard is set, allow extra suffix.
            if (rule->tail_wildcard && ((__u32)t + 1U == rule->token_count)) {
                if (__match_qmark_bounded(rule->token[t], &name[pos], len) != 0) {
                    return 0;
                }
                return 1;
            }

            if (__match_qmark_bounded(rule->token[t], &name[pos], len) != 0) {
                return 0;
            }
            pos += len;
            continue;
        }

        if (tt == BASENAME_TOKEN_DIGIT1) {
            if (pos >= name_len) {
                return 0;
            }
            char c = name[pos];
            if (c < '0' || c > '9') {
                return 0;
            }
            pos += 1;
            continue;
        }

        if (tt == BASENAME_TOKEN_DIGITSPLUS) {
            if (pos >= name_len) {
                return 0;
            }
            if (name[pos] < '0' || name[pos] > '9') {
                return 0;
            }

            __u32 digit_count = 0;
            for (int i = 0; i < 32; i++) {
                __u32 idx = pos + (__u32)i;
                if (idx >= name_len) {
                    break;
                }
                char dc = name[idx];
                if (dc >= '0' && dc <= '9') {
                    digit_count++;
                    continue;
                }
                break;
            }
            if (digit_count < 1) {
                return 0;
            }
            pos += digit_count;
            continue;
        }
    }

    return pos == name_len;
}

#endif /* __BPF_PATH_MATCH_H__ */
