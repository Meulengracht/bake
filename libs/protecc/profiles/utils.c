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

#include <chef/platform.h>

#include <protecc/profile.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>

#include "../private.h"

char* __blob_string_dup(const uint8_t* strings, uint32_t offset)
{
    if (offset == PROTECC_PROFILE_STRING_NONE) {
        return NULL;
    }
    return platform_strdup((const char*)(strings + offset));
}

uint32_t __blob_string_write(uint8_t* base, size_t* cursor, const char* value)
{
    uint32_t offset;
    size_t   length;

    if (cursor == NULL || value == NULL) {
        return PROTECC_PROFILE_STRING_NONE;
    }

    length = strlen(value) + 1u;

    if (base != NULL) {
        memcpy(base + *cursor, value, length);
    }

    if (*cursor > UINT32_MAX) {
        return PROTECC_PROFILE_STRING_NONE;
    }

    offset = (uint32_t)(*cursor);
    *cursor += length;
    return offset;
}

size_t __blob_string_measure(const char* value)
{
    return value ? (strlen(value) + 1u) : 0u;
}

const char* __blob_string_ptr(const uint8_t* strings, uint32_t offset)
{
    if (offset == PROTECC_PROFILE_STRING_NONE) {
        return NULL;
    }
    return (const char*)(strings + offset);
}
