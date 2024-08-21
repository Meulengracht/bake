/**
 * Copyright 2022, Philip Meulengracht
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

#include <errno.h>
#include <chef/platform.h>
#include <string.h>

char* strflatten(const char* const* values, char* sep, size_t* lengthOut)
{
    char*  flat;
    size_t flatLength = 1; // nil terminator
    size_t sepLength = strlen(sep);

    for (int i = 0; values[i] != NULL; i++) {
        flatLength += strlen(values[i]);
        if (values[i + 1]) {
            flatLength += sepLength;
        }
        i++;
    }

    flat = calloc(flatLength, 1);
    if (flat == NULL) {
        return NULL;
    }

    for (int i = 0, j = 0; values[i] != NULL; i++) {
        size_t len = strlen(values[i]);
        
        memcpy(&flat[j], values[i], len);
        j += len;

        if (values[i + 1]) {
            memcpy(&flat[j], sep, sepLength);
            j += sepLength;
        }
        i++;
    }
    *lengthOut = flatLength;
    return flat;
}
