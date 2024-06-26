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
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

wchar_t* mb_to_wcs(const char* path) 
{
    size_t pathLength = strlen(path);
    wchar_t* wpath = (wchar_t*)calloc(sizeof(wchar_t), pathLength + 10);
    if (wpath != NULL) {
        mbstowcs(wpath, path, pathLength);
    }
    return wpath;
}

int platform_mkdir(const char* path) 
{
    wchar_t* wpath = mb_to_wcs(path);
    if (!wpath) {
        errno = ENOMEM;
        return -1;
    }

    size_t length = wcslen(wpath);
    if (wpath[length - 1] == L'\\' || wpath[length - 1] == L'/') {
        wpath[length - 1] = L'\0';
    }

    wchar_t* p;
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

    int status = _wmkdir(wpath);
    if (status != 0 && errno != EEXIST) {
        free(wpath);
        return -1;
    }

    free(wpath);
    return 0;
}
