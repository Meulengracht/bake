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

static bool __charclass_match(const char* pattern, size_t* consumed, char value)
{
    size_t index = 1;
    bool   invert = false;
    bool   matched = false;
    int    foldedC = (unsigned char)value;

    if (pattern[index] == '!' || pattern[index] == '^') {
        invert = true;
        index++;
    }

    if (pattern[index] == ']') {
        int first = (unsigned char)pattern[index];
        if (first == foldedC) {
            matched = true;
        }
        index++;
    }

    while (pattern[index] && pattern[index] != ']') {
        int first = (unsigned char)pattern[index];

        if (pattern[index + 1] == '-' && pattern[index + 2] && pattern[index + 2] != ']') {
            int last = (unsigned char)pattern[index + 2];
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

static bool __glob_match(const char* pattern, const char* value)
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
            if (__charclass_match(pattern + patternIndex, &consumed, value[valueIndex])) {
                patternIndex += consumed;
                valueIndex++;
                continue;
            }
        } else if ((unsigned char)pattern[patternIndex] == (unsigned char)value[valueIndex]) {
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

bool __match_optional_pattern(const char* pattern, const char* value)
{
    if (pattern == NULL) {
        return true;
    }
    if (value == NULL) {
        return false;
    }
    return __glob_match(pattern, value);
}
