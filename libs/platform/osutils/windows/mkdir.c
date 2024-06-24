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

// https://stackoverflow.com/questions/1530760/how-do-i-recursively-create-a-folder-in-win32
static int __directory_exists(LPCTSTR szPath)
{
    DWORD dwAttrib = GetFileAttributesW(szPath);

    if (dwAttrib == INVALID_FILE_ATTRIBUTES) {
        return 0; // Path does not exist
    }

    if (dwAttrib & FILE_ATTRIBUTE_DIRECTORY) {
        return 1; // Path is a directory
    }

    return 0; // Path exists but is not a directory
}


wchar_t* mb_to_wcs(const char* path) {
    int size_char = MultiByteToWideChar(CP_UTF8, 0, path, -1, NULL, 0);
    wchar_t* wpath = (wchar_t*)malloc(size_char * sizeof(wchar_t));
    if (wpath) {
        MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, size_char);
    }
    return wpath;
}

static int __mkdir(const char* path) {    
    int status = __directory_exists(path);
    if (!status) {
        wchar_t* wpath = mb_to_wcs(path);
        if (wpath) {
            if (CreateDirectoryExW(NULL, wpath, NULL)) {
                free(wpath);
                return 0;
            } else {
                free(wpath);
                return -1;
            }
        }
        else {
            return -1;
        }
    }
    return status == 1 ? 0 : -1;
}

int platform_mkdir(const char* path)
{
    wchar_t *wpath = mb_to_wcs(path);
    char*  p = NULL;
    size_t length;
    int    status;

    status = snprintf(wpath, sizeof(wpath), "%s", path);
    if (status >= sizeof(wpath)) {
        errno = ENAMETOOLONG;
        return -1; 
    }

    length = strlen(wpath);
    if (wpath[length - 1] == '\\' || wpath[length - 1] == '/') {
        wpath[length - 1] = 0;
    }

    for (p = wpath + 1; *p; p++) {
        if (*p == '\\' || *p == '/') {
            *p = 0;
            status = __mkdir(wpath);
            if (status) {
                return status;
            }

            *p = '\\';
        }
    }
    return __mkdir(wpath);
}
