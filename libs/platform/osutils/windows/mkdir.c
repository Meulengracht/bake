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
    DWORD dwAttrib = GetFileAttributes(szPath);

    if (dwAttrib == INVALID_FILE_ATTRIBUTES) {
        return 0; // Path does not exist
    }

    if (dwAttrib & FILE_ATTRIBUTE_DIRECTORY) {
        return 1; // Path is a directory
    }

    return 0; // Path exists but is not a directory
}

static int __mkdir(const char* path) {
    int status = __directory_exists(path);
    if (!status) {
        if (CreateDirectory(path, NULL)) {
            return 0;
        } else {
            return -1;
        }
    }
    return status == 1 ? 0 : -1;
}

int platform_mkdir(const char* path)
{
    char   ccpath[512];
    char*  p = NULL;
    size_t length;
    int    status;

    status = snprintf(ccpath, sizeof(ccpath), "%s", path);
    if (status >= sizeof(ccpath)) {
        errno = ENAMETOOLONG;
        return -1; 
    }

    length = strlen(ccpath);
    if (ccpath[length - 1] == '\\' || ccpath[length - 1] == '/') {
        ccpath[length - 1] = 0;
    }

    for (p = ccpath + 1; *p; p++) {
        if (*p == '\\' || *p == '/') {
            *p = 0;
            
            status = __mkdir(ccpath);
            if (status) {
                return status;
            }

            *p = '\\';
        }
    }
    return __mkdir(ccpath);
}
