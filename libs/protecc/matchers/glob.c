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
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

#include "../private.h"

static int __char_fold(int c, bool caseInsensitive)
{
    return caseInsensitive ? tolower((unsigned char)c) : c;
}

static bool __charclass_match(const char* pattern, size_t* consumed, char value, bool caseInsensitive)
{
    size_t index = 1;
    bool   invert = false;
    bool   matched = false;
    int    foldedC = __char_fold(value, caseInsensitive);

    if (pattern[index] == '!' || pattern[index] == '^') {
        invert = true;
        index++;
    }

    if (pattern[index] == ']') {
        int first = __char_fold(pattern[index], caseInsensitive);
        if (first == foldedC) {
            matched = true;
        }
        index++;
    }

    while (pattern[index] && pattern[index] != ']') {
        int first = __char_fold(pattern[index], caseInsensitive);

        if (pattern[index + 1] == '-' && pattern[index + 2] && pattern[index + 2] != ']') {
            int last = __char_fold(pattern[index + 2], caseInsensitive);
            if (first <= foldedC && foldedC <= last) {
                matched = true;
            }
            index += 3;
            continue;
        }

        if (first == foldedC) {
            matched = true;
        }
        index++;
    }

    if (pattern[index] != ']') {
        *consumed = 1;
        return pattern[0] == value;
    }

    *consumed = index + 1;
    return invert ? !matched : matched;
}

static bool __glob_match(const char* pattern, const char* value, bool caseInsensitive)
{
    size_t patternIndex = 0;
    size_t valueIndex = 0;
    size_t starPattern = SIZE_MAX;
    size_t starValue = SIZE_MAX;

    while (value[valueIndex]) {
        if (pattern[patternIndex] == '*') {
            while (pattern[patternIndex] == '*') {
                patternIndex++;
            }
            starPattern = patternIndex;
            starValue = valueIndex;
            continue;
        }

        if (pattern[patternIndex] == '?') {
            patternIndex++;
            valueIndex++;
            continue;
        }

        if (pattern[patternIndex] == '[') {
            size_t consumed = 0;
            if (__charclass_match(pattern + patternIndex, &consumed, value[valueIndex], caseInsensitive)) {
                patternIndex += consumed;
                valueIndex++;
                continue;
            }
        } else if (__char_fold(pattern[patternIndex], caseInsensitive)
                == __char_fold(value[valueIndex], caseInsensitive)) {
            patternIndex++;
            valueIndex++;
            continue;
        }

        if (starPattern != SIZE_MAX) {
            patternIndex = starPattern;
            starValue++;
            valueIndex = starValue;
            continue;
        }

        return false;
    }

    while (pattern[patternIndex] == '*') {
        patternIndex++;
    }

    return pattern[patternIndex] == '\0';
}

bool __match_optional_pattern(const char* pattern, const char* value, bool caseInsensitive)
{
    if (pattern == NULL) {
        return true;
    }
    if (value == NULL) {
        return false;
    }
    return __glob_match(pattern, value, caseInsensitive);
}
