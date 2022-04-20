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
#include <libplatform.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char* strpathcombine(const char* path1, const char* path2)
{
    char*  combined;
    int    status;
    size_t path1Length;
    size_t path2Length;

    if (path1 == NULL) {
        return strdup(path2);
    }

    if (path2 == NULL) {
        return strdup(path1);
    }

    path1Length = strlen(path1);
    path2Length = strlen(path2);

    if (path1Length == 0) {
        return strdup(path2);
    }

    if (path2Length == 0) {
        return strdup(path1);
    }

    combined = malloc(path1Length + path2Length + 2);
    if (combined == NULL) {
        return NULL;
    }

    if (path1[path1Length - 1] != '/') {
        status = sprintf(combined, "%s/%s", path1, path2);
    } else {
        status = sprintf(combined, "%s%s", path1, path2);
    }

    if (status < 0) {
        free(combined);
        return NULL;
    }
    return combined;
}
