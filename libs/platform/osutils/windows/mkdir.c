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

int __directory_exists(const wchar_t* path) 
{
    DWORD attribs = GetFileAttributesW(path);
    if (attribs == INVALID_FILE_ATTRIBUTES) {
        return 0;
    }
    return (attribs & FILE_ATTRIBUTE_DIRECTORY) ? 1 : -1;
}

wchar_t* mb_to_wcs(const char* path) 
{
    size_t length = strlen(path) + 1;
    wchar_t* wpath = (wchar_t*)malloc((length + 1) * sizeof(wchar_t));
    if (wpath) {
        mbstowcs(wpath, path, length);
    }
    return wpath;
}

static int __mkdir(const char* path) 
{
    wchar_t* wpath = mb_to_wcs(path);
    if (!wpath) {
        return -1;
    }

    int status = __directory_exists(wpath);
    if (status == 1) {
        free(wpath);
        return 0;
    }

    char* sub_path = _strdup(path);
    if (!sub_path) {
        free(wpath);
        return -1;
    }

    char* last_separator = strrchr(sub_path, '\\');
    if (last_separator != NULL) {
        *last_separator = '\0';
        if (__mkdir(sub_path) != 0) {
            free(sub_path);
            free(wpath);
            return -1;
        }
    }
    free(sub_path);

    if (CreateDirectoryW(wpath, NULL) || GetLastError() == ERROR_ALREADY_EXISTS) {
        free(wpath);
        return 0;
    } else {
        DWORD error = GetLastError();
        printf("Error creating directory: %lu\n", error);
        free(wpath);
        return -1;
    }
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
    for (p = wpath + 1; *p; p++) {
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
