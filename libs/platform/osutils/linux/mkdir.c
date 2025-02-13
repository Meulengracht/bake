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
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static int __directory_exists(
    const char* path)
{
    struct stat st;
    if (stat(path, &st)) {
        if (errno == ENOENT) {
            return 0;
        }
        return -1;
    }
    return S_ISDIR(st.st_mode) ? 1 : -1;
}

static int __mkdir(const char* path, int perms)
{
    int status = __directory_exists(path);
    if (!status) {
        return mkdir(path, perms);
    }
    return status == 1 ? 0 : -1;
}

// original http://stackoverflow.com/a/2336245/119527
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
    if (ccpath[length - 1] == '/') {
        ccpath[length - 1] = 0;
    }

    for (p = ccpath + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            
            status = __mkdir(ccpath, S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
            if (status) {
                return status;
            }

            *p = '/';
        }
    }
    return __mkdir(ccpath, S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
}
