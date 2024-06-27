/**
 * Copyright 2024, Philip Meulengracht
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
#include <stdlib.h>

char* platform_abspath(const char* path)
{
    char* fullPath;
    DWORD result;

    fullPath = calloc(1, MAX_PATH);
    if (fullPath == NULL) {
        return NULL;
    }

    result = GetFullPathName(path, MAX_PATH, fullPath, NULL);
    if (!result) {
        free(fullPath);
        return NULL;
    }
    return fullPath;
}
