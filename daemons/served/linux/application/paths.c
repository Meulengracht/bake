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
#include <application.h>
#include <chef/platform.h>
#include <linux/limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <utils.h>

int served_application_ensure_paths(struct served_application* application)
{
    char* path;

    path = malloc(PATH_MAX);
    if (path == NULL) {
        return -1;
    }

    sprintf(path, "/usr/share/chef/%s", application->name);
    if (platform_mkdir(path) != 0) {
        free(path);
        return -1;
    }

    sprintf(path, "/usr/share/chef/%s/%i", application->name, application->revision);
    if (platform_mkdir(path) != 0) {
        free(path);
        return -1;
    }
    free(path);
    return 0;
}

const char* served_application_get_pack_path(struct served_application* application)
{
    char* path;
    int   status;

    path = malloc(PATH_MAX);
    if (path == NULL) {
        return NULL;
    }

    status = platform_getuserdir(path, PATH_MAX);
    if (status != 0) {
        free(path);
        return NULL;
    }

    strcat(path, CHEF_PATH_SEPARATOR_S ".chef" CHEF_PATH_SEPARATOR_S "packs" CHEF_PATH_SEPARATOR_S);
    strcat(path, application->name);
    return path;
}

const char* served_application_get_mount_path(struct served_application* application)
{
    char* path;

    path = malloc(PATH_MAX);
    if (path == NULL) {
        return NULL;
    }

    sprintf(path, "/run/chef/%s", application->name);
    return path;
}

const char* served_application_get_data_path(struct served_application* application)
{
    char* path;

    path = malloc(PATH_MAX);
    if (path == NULL) {
        return NULL;
    }

    sprintf(path, "/usr/share/chef/%s/%i", application->name, application->revision);
    return path;
}
