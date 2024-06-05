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
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t __append_path_part(char* path, size_t pathLength, const char* part)
{
    char*       current = path;
    const char* toAdd   = part;
    size_t      i       = pathLength;

    // ensure path always ends on a '/' before adding the next part
    if (i > 0 && current[i - 1] != CHEF_PATH_SEPARATOR) {
        current[i++] = CHEF_PATH_SEPARATOR;
    }

    while (*toAdd == CHEF_PATH_SEPARATOR) {
        toAdd++;
    }

    while (*toAdd) {
        current[i++] = *toAdd;
        toAdd++;
    }
    return i - pathLength;
}

char* strpathjoin(const char* base, ...)
{
    char*   joined;
    va_list args;
    size_t  len;

    if (base == NULL) {
        return NULL;
    }
    len = strlen(base);

    joined = malloc(4096);
    if (joined == NULL) {
        return NULL;
    }
    memset(joined, 0, 4096);
    strcpy(joined, base);

    va_start(args, base);
    while (1) {
        const char* part = va_arg(args, const char*);
        size_t      partLen;
        if (part == NULL) {
            break;
        }

        partLen = strlen(part);
        if ((partLen + len) > 4095) {
            free(joined);
            errno = E2BIG;
            return NULL;
        }

        // this returns the actual number of bytes copied, which can
        // be less than the actual length of the part
        partLen = __append_path_part(joined, len, part);
        len += partLen;
    }
    va_end(args);
    return joined;
}

char* strpathcombine(const char* path1, const char* path2)
{
    char*  combined;
    int    status;
    size_t path1Length;
    size_t path2Length;

    if (path1 == NULL && path2 == NULL) {
        return NULL;
    }

    if (path1 == NULL) {
        return strdup(path2);
    } else if (path2 == NULL) {
        return strdup(path1);
    }

    path1Length = strlen(path1);
    path2Length = strlen(path2);

    if (path1Length == 0) {
        return strdup(path2);
    } else if (path2Length == 0) {
        return strdup(path1);
    }

    if (path2[0] == CHEF_PATH_SEPARATOR) {
        path2++;
        path2Length--;
    };

    combined = malloc(path1Length + path2Length + 2);
    if (combined == NULL) {
        return NULL;
    }

    if (path1[path1Length - 1] != CHEF_PATH_SEPARATOR) {
        status = sprintf(combined, "%s" CHEF_PATH_SEPARATOR_S "%s", path1, path2);
    } else {
        status = sprintf(combined, "%s%s", path1, path2);
    }

    if (status < 0) {
        free(combined);
        return NULL;
    }
    return combined;
}
