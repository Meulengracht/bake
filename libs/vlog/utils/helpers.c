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

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char* vlog_strdup(const char* text)
{
    char*  copy;
    size_t length;

    if (text == NULL) {
        return NULL;
    }

    length = strlen(text) + 1;
    copy = malloc(length);
    if (copy == NULL) {
        return NULL;
    }
    memcpy(copy, text, length);
    return copy;
}

char* vlog_vformat(const char* format, va_list args)
{
    va_list copy;
    char*   buffer;
    int     length;

    if (format == NULL) {
        return NULL;
    }

    va_copy(copy, args);
    length = vsnprintf(NULL, 0, format, copy);
    va_end(copy);
    if (length < 0) {
        return NULL;
    }

    buffer = malloc((size_t)length + 1);
    if (buffer == NULL) {
        return NULL;
    }

    vsnprintf(buffer, (size_t)length + 1, format, args);
    return buffer;
}
