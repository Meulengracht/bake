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
#include <direct.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

static wchar_t* __mkdir_start(wchar_t* path)
{
    wchar_t* current;

    if (path == NULL) {
        return NULL;
    }

    if (((path[0] >= L'A' && path[0] <= L'Z') || (path[0] >= L'a' && path[0] <= L'z')) && path[1] == L':') {
        if (path[2] == L'\\' || path[2] == L'/') {
            return path + 3;
        }
        return path + 2;
    }

    if ((path[0] == L'\\' || path[0] == L'/') && (path[1] == L'\\' || path[1] == L'/')) {
        current = path + 2;
        while (*current && *current != L'\\' && *current != L'/') {
            current++;
        }
        if (*current) {
            current++;
            while (*current && *current != L'\\' && *current != L'/') {
                current++;
            }
            if (*current) {
                return current + 1;
            }
        }
        return current;
    }

    if (path[0] == L'\\' || path[0] == L'/') {
        return path + 1;
    }

    return path + 1;
}

static wchar_t* __mbtowc(const char* path) 
{
    size_t   pathLength = strlen(path);
    wchar_t* wpath = (wchar_t*)calloc(pathLength + 10, sizeof(wchar_t));
    if (wpath != NULL) {
        mbstowcs(wpath, path, pathLength);
    }
    return wpath;
}

int platform_mkdir(const char* path) 
{
    size_t   length;
    wchar_t* current;
    int      status;
    wchar_t* wpath = __mbtowc(path);
    
    if (wpath == NULL) {
        return -1;
    }

    length = wcslen(wpath);
    if (wpath[length - 1] == L'\\' || wpath[length - 1] == L'/') {
        wpath[length - 1] = L'\0';
    }

    current = __mkdir_start(wpath);
    for (wchar_t* p = current; *p; p++) {
        if (*p == L'\\' || *p == L'/') {
            wchar_t separator = *p;

            *p = L'\0';
            if (_wmkdir(wpath) != 0 && errno != EEXIST) {
                free(wpath);
                return -1;
            }
            *p = separator;
        }
    }

    status = _wmkdir(wpath);
    if (status && errno != EEXIST) {
        free(wpath);
        return -1;
    }

    free(wpath);
    return 0;
}
