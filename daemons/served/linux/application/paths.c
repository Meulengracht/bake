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

#include <application.h>
#include <chef/platform.h>
#include <linux/limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vlog.h>

int served_application_ensure_paths(struct served_application* application)
{
    char* path;

    path = malloc(PATH_MAX);
    if (path == NULL) {
        return -1;
    }

    sprintf(path, "/usr/share/chef/%s-%s",
        application->publisher, application->package);
    if (platform_mkdir(path) != 0) {
        VLOG_ERROR("paths", "failed to create path %s\n", path);
        free(path);
        return -1;
    }

    sprintf(path, "/usr/share/chef/%s-%s/%i", application->publisher,
        application->package, application->revision);
    if (platform_mkdir(path) != 0) {
        VLOG_ERROR("paths", "failed to create path %s\n", path);
        free(path);
        return -1;
    }
    free(path);
    return 0;
}

const char* served_application_get_pack_path(struct served_application* application)
{
    char* path;

    path = malloc(PATH_MAX);
    if (path == NULL) {
        return NULL;
    }

    sprintf(&path[0], "/var/chef/packs/%s-%s.pack", application->publisher, application->package);
    return path;
}

const char* served_application_get_mount_path(struct served_application* application)
{
    char* path;

    path = malloc(PATH_MAX);
    if (path == NULL) {
        return NULL;
    }

    sprintf(path, "/run/chef/%s-%s", application->publisher, application->package);
    return path;
}

const char* served_application_get_data_path(struct served_application* application)
{
    char* path;

    path = malloc(PATH_MAX);
    if (path == NULL) {
        return NULL;
    }

    sprintf(path, "/usr/share/chef/%s-%s/%i", application->publisher,
        application->package, application->revision);
    return path;
}
