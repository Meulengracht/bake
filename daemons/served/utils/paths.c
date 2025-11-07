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

#include <utils.h>
#include <chef/platform.h>
#include <errno.h>
#include <linux/limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vlog.h>

char* served_paths_path(const char* path)
{
#ifdef CHEF_AS_SNAP
    // /var/snap/<snap>/common
    char* val = getenv("SNAP_COMMON");
    if (val != NULL) {
        return strpathcombine(val, path);
    }
#else
    return platform_strdup(path);
#endif
}

int served_application_ensure_paths(const char* publisher, const char* package)
{
    char  tmp[PATH_MAX];
    char* path;

    // always make sure mount-point is created
    snprintf(
        &tmp[0], sizeof(tmp) - 1, 
        "/var/chef/mnt/%s-%s",
        publisher, package
    );
    path = served_paths_path(&tmp[0]);
    if (platform_mkdir(path) != 0) {
        // so we might receive ENOTCONN here, which means 'Transport endpoint is not connected'
        // but we can safely ignore this error
        if (errno != ENOTCONN) {
            VLOG_ERROR("paths", "failed to create mount path %s\n", path);
            free(path);
            return -1;
        }
    }
    free(path);
    return 0;
}

char* utils_path_pack(const char* publisher, const char* package)
{
    char buffer[PATH_MAX];
    snprintf(
        &buffer[0], sizeof(buffer),
        "/var/chef/packs/%s-%s.pack",
        publisher, package
    );
    return served_paths_path(&buffer[0]);
}

char* utils_path_mount(const char* publisher, const char* package)
{
    char buffer[PATH_MAX];
    snprintf(
        &buffer[0], sizeof(buffer), 
        "/var/chef/mnt/%s-%s", 
        publisher, package
    );
    return served_paths_path(&buffer[0]);
}

char* utils_path_data(const char* publisher, const char* package, int revision)
{
    char buffer[PATH_MAX];
    snprintf(
        &buffer[0], sizeof(buffer), 
        "/usr/share/chef/%s-%s/%i", 
        publisher, package, revision
    );
    return served_paths_path(&buffer[0]);
}

char* utils_path_command_symlink(const char* name)
{
    char* path;
    int   status;

    path = malloc(PATH_MAX);
    if (path == NULL) {
        return NULL;
    }
    
    sprintf(path, "/chef/bin/%s", name);
    return path;
}
