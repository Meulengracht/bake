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

protecc_error_t __blob_string_offset_validate(
    uint32_t       offset,
    const uint8_t* strings,
    size_t         stringsSize)
{
    const uint8_t* end;

    if (offset == PROTECC_PROFILE_STRING_NONE) {
        return PROTECC_OK;
    }

    if (!strings || offset >= stringsSize) {
        return PROTECC_ERROR_INVALID_BLOB;
    }

    end = memchr(strings + offset, '\0', stringsSize - offset);
    if (!end) {
        return PROTECC_ERROR_INVALID_BLOB;
    }

    return PROTECC_OK;
}

static void __charclass_bitmap_set(uint8_t bitmap[PROTECC_PROFILE_CHARCLASS_BITMAP_SIZE], unsigned char c)
{
    bitmap[c >> 3u] |= (uint8_t)(1u << (c & 7u));
}

static bool __charclass_parse(
    const char*                        pattern,
    size_t                             start_index,
    protecc_profile_charclass_entry_t* entry_out)
{
    size_t  cursor;
    bool    invert;
    uint8_t working[PROTECC_PROFILE_CHARCLASS_BITMAP_SIZE] = {0};

    if (pattern == NULL || entry_out == NULL) {
        return false;
    }

    if (pattern[start_index] != '[' || pattern[start_index + 1u] == '\0') {
        return false;
    }

    invert = false;
    cursor = start_index + 1u;

    if (pattern[cursor] == '!' || pattern[cursor] == '^') {
        invert = true;
        cursor++;
        if (pattern[cursor] == '\0') {
            return false;
        }
    }

    if (pattern[cursor] == ']') {
        unsigned char value = (unsigned char)pattern[cursor];
        __charclass_bitmap_set(working, value);
        cursor++;
    }

    for (;;) {
        char first;
        char dash;
        char last;

        first = pattern[cursor];
        if (first == '\0') {
            return false;
        }

        if (first == ']') {
            cursor++;
            break;
        }

        dash = pattern[cursor + 1u];
        last = pattern[cursor + 2u];

        if (dash == '-' && last != '\0' && last != ']') {
            unsigned char left = (unsigned char)first;
            unsigned char right = (unsigned char)last;

            if (left <= right) {
                unsigned char value = left;
                for (;;) {
                    __charclass_bitmap_set(working, value);
                    if (value == right) {
                        break;
                    }
                    value++;
                }
            }

            cursor += 3u;
            continue;
        }

        {
            unsigned char value = (unsigned char)first;
            __charclass_bitmap_set(working, value);
        }

        cursor++;
    }

    if (cursor <= start_index) {
        return false;
    }

    if ((cursor - start_index) > UINT16_MAX) {
        return false;
    }

    entry_out->pattern_off = 0;
    entry_out->consumed = (uint16_t)(cursor - start_index);
    entry_out->reserved[0] = 0;
    entry_out->reserved[1] = 0;

    if (invert) {
        for (size_t i = 0; i < PROTECC_PROFILE_CHARCLASS_BITMAP_SIZE; i++) {
            entry_out->bitmap[i] = (uint8_t)~working[i];
        }
    } else {
        memcpy(entry_out->bitmap, working, sizeof(working));
    }

    return true;
}

static protecc_error_t __charclass_table_reserve(
    protecc_charclass_table_t* table,
    size_t                     additional)
{
    size_t needed;
    size_t new_capacity;
    void*  resized;

    if (table == NULL) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    needed = table->count + additional;
    if (needed <= table->capacity) {
        return PROTECC_OK;
    }

    new_capacity = table->capacity == 0 ? 8u : table->capacity;
    while (new_capacity < needed) {
        new_capacity *= 2u;
    }

    resized = realloc(table->entries, new_capacity * sizeof(protecc_profile_charclass_entry_t));
    if (resized == NULL) {
        return PROTECC_ERROR_OUT_OF_MEMORY;
    }

    table->entries = (protecc_profile_charclass_entry_t*)resized;
    table->capacity = new_capacity;
    return PROTECC_OK;
}

protecc_error_t __charclass_collect(
    const char*               pattern,
    uint32_t                  pattern_offset,
    protecc_charclass_table_t* table)
{
    size_t length;

    if (table == NULL) {
        return PROTECC_ERROR_INVALID_ARGUMENT;
    }

    if (pattern == NULL || pattern_offset == PROTECC_PROFILE_STRING_NONE) {
        return PROTECC_OK;
    }

    length = strlen(pattern);
    for (size_t i = 0; i < length; i++) {
        protecc_profile_charclass_entry_t entry;
        uint64_t                          absolute;
        protecc_error_t                   err;

        if (pattern[i] != '[') {
            continue;
        }

        memset(&entry, 0, sizeof(entry));
        if (!__charclass_parse(pattern, i, &entry)) {
            continue;
        }

        absolute = (uint64_t)pattern_offset + (uint64_t)i;
        if (absolute > UINT32_MAX) {
            return PROTECC_ERROR_INVALID_ARGUMENT;
        }

        if (table->count >= PROTECC_PROFILE_MAX_CHAR_CLASSES) {
            return PROTECC_ERROR_INVALID_ARGUMENT;
        }

        err = __charclass_table_reserve(table, 1u);
        if (err != PROTECC_OK) {
            return err;
        }

        entry.pattern_off = (uint32_t)absolute;
        table->entries[table->count++] = entry;
    }

    return PROTECC_OK;
}

void __charclass_table_free(protecc_charclass_table_t* table)
{
    if (table == NULL) {
        return;
    }

    free(table->entries);
    table->entries = NULL;
    table->count = 0;
    table->capacity = 0;
}
