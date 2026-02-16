/**
 * @file matcher.c
 * @brief Iterative pattern matching implementation for protecc library
 */

#include "protecc_internal.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    size_t pattern_pos;
    size_t path_pos;
} match_state_t;

static char normalize_char(char c, bool case_insensitive) {
    if (!case_insensitive) {
        return c;
    }
    return (char)tolower((unsigned char)c);
}

static bool charset_contains(
    const char* pattern,
    size_t start,
    size_t end,
    char c,
    bool case_insensitive
) {
    char value = normalize_char(c, case_insensitive);
    size_t i = start;

    while (i < end) {
        if (i + 2 < end && pattern[i + 1] == '-') {
            char range_start = normalize_char(pattern[i], case_insensitive);
            char range_end = normalize_char(pattern[i + 2], case_insensitive);
            if (range_start <= value && value <= range_end) {
                return true;
            }
            i += 3;
        } else {
            if (normalize_char(pattern[i], case_insensitive) == value) {
                return true;
            }
            i++;
        }
    }

    return false;
}

static bool push_state(
    match_state_t** stack,
    size_t* stack_size,
    size_t* stack_capacity,
    bool* visited,
    size_t pattern_len,
    size_t path_len,
    size_t pattern_pos,
    size_t path_pos
) {
    if (pattern_pos > pattern_len || path_pos > path_len) {
        return false;
    }

    size_t cols = path_len + 1;
    size_t index = pattern_pos * cols + path_pos;
    if (visited[index]) {
        return true;
    }
    visited[index] = true;

    if (*stack_size >= *stack_capacity) {
        size_t new_capacity = (*stack_capacity == 0) ? 64 : (*stack_capacity * 2);
        match_state_t* new_stack = realloc(*stack, new_capacity * sizeof(match_state_t));
        if (!new_stack) {
            return false;
        }
        *stack = new_stack;
        *stack_capacity = new_capacity;
    }

    (*stack)[*stack_size].pattern_pos = pattern_pos;
    (*stack)[*stack_size].path_pos = path_pos;
    (*stack_size)++;

    return true;
}

bool protecc_match_pattern(
    const char* pattern,
    const char* path,
    size_t path_len,
    uint32_t flags
) {
    if (!pattern || !path) {
        return false;
    }

    if (path_len == 0) {
        path_len = strlen(path);
    }

    size_t pattern_len = strlen(pattern);
    if (path_len == SIZE_MAX || (pattern_len + 1) > (SIZE_MAX / (path_len + 1))) {
        /* Guard visited_size = (pattern_len + 1) * (path_len + 1) from overflow. */
        return false;
    }
    size_t visited_size = (pattern_len + 1) * (path_len + 1);
    bool* visited = calloc(visited_size, sizeof(bool));
    if (!visited) {
        return false;
    }

    match_state_t* stack = NULL;
    size_t stack_size = 0;
    size_t stack_capacity = 0;
    bool case_insensitive = (flags & PROTECC_FLAG_CASE_INSENSITIVE) != 0;
    bool matched = false;

    if (!push_state(
            &stack, &stack_size, &stack_capacity, visited, pattern_len, path_len, 0, 0)) {
        goto cleanup;
    }

    while (stack_size > 0) {
        match_state_t state = stack[--stack_size];
        size_t ppos = state.pattern_pos;
        size_t spos = state.path_pos;

        if (ppos == pattern_len) {
            if (spos == path_len) {
                matched = true;
                break;
            }
            continue;
        }

        if (pattern[ppos] == '*' && (ppos + 1) < pattern_len && pattern[ppos + 1] == '*') {
            size_t next = ppos + 2;
            if (next < pattern_len && pattern[next] == '/') {
                next++;
            }
            for (size_t try_pos = spos; try_pos <= path_len; try_pos++) {
                if (!push_state(
                        &stack,
                        &stack_size,
                        &stack_capacity,
                        visited,
                        pattern_len,
                        path_len,
                        next,
                        try_pos)) {
                    goto cleanup;
                }
            }
            continue;
        }

        if (pattern[ppos] == '*') {
            size_t next = ppos + 1;
            size_t try_pos = spos;
            while (try_pos <= path_len) {
                if (!push_state(
                        &stack,
                        &stack_size,
                        &stack_capacity,
                        visited,
                        pattern_len,
                        path_len,
                        next,
                        try_pos)) {
                    goto cleanup;
                }
                if (try_pos < path_len && path[try_pos] == '/') {
                    break;
                }
                try_pos++;
            }
            continue;
        }

        if (pattern[ppos] == '?') {
            if (spos < path_len) {
                if (!push_state(
                        &stack,
                        &stack_size,
                        &stack_capacity,
                        visited,
                        pattern_len,
                        path_len,
                        ppos + 1,
                        spos + 1)) {
                    goto cleanup;
                }
            }
            continue;
        }

        if (pattern[ppos] == '[') {
            size_t close = ppos + 1;
            while (close < pattern_len && pattern[close] != ']') {
                close++;
            }
            if (close >= pattern_len) {
                continue;
            }

            size_t next = close + 1;
            char modifier = '\0';
            if (next < pattern_len && (pattern[next] == '?' || pattern[next] == '+' || pattern[next] == '*')) {
                modifier = pattern[next];
                next++;
            }

            bool can_match_current = (spos < path_len) &&
                charset_contains(pattern, ppos + 1, close, path[spos], case_insensitive);

            if (modifier == '\0') {
                if (can_match_current) {
                    if (!push_state(
                            &stack,
                            &stack_size,
                            &stack_capacity,
                            visited,
                            pattern_len,
                            path_len,
                            next,
                            spos + 1)) {
                        goto cleanup;
                    }
                }
                continue;
            }

            if (modifier == '?' || modifier == '*') {
                if (!push_state(
                        &stack,
                        &stack_size,
                        &stack_capacity,
                        visited,
                        pattern_len,
                        path_len,
                        next,
                        spos)) {
                    goto cleanup;
                }
            }

            if (modifier == '+' || modifier == '*' || modifier == '?') {
                if (!can_match_current) {
                    continue;
                }

                size_t idx = spos;
                while (idx < path_len &&
                       charset_contains(pattern, ppos + 1, close, path[idx], case_insensitive)) {
                    if (!push_state(
                            &stack,
                            &stack_size,
                            &stack_capacity,
                            visited,
                            pattern_len,
                            path_len,
                            next,
                            idx + 1)) {
                        goto cleanup;
                    }
                    if (modifier == '?') {
                        break;
                    }
                    idx++;
                }
            }
            continue;
        }

        if (spos < path_len &&
            normalize_char(pattern[ppos], case_insensitive) ==
                normalize_char(path[spos], case_insensitive)) {
            if (!push_state(
                    &stack,
                    &stack_size,
                    &stack_capacity,
                    visited,
                    pattern_len,
                    path_len,
                    ppos + 1,
                    spos + 1)) {
                goto cleanup;
            }
        }
    }

cleanup:
    free(stack);
    free(visited);
    return matched;
}
