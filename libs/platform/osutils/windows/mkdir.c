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
    int      status;
    wchar_t* wpath = __mbtowc(path);
    
    if (wpath == NULL) {
        return -1;
    }

    length = wcslen(wpath);
    if (wpath[length - 1] == L'\\' || wpath[length - 1] == L'/') {
        wpath[length - 1] = L'\0';
    }

    for (wchar_t* p = wpath + 1; *p; p++) {
        if (*p == L'\\' || *p == L'/') {
            *p = L'\0';
            if (_wmkdir(wpath) != 0 && errno != EEXIST) {
                free(wpath);
                return -1;
            }
            *p = L'\\';
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
