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
#include <libplatform.h>
#include <linux/limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <utils.h>
#include <unistd.h>

static const char* __get_command_symlink_path(struct served_command* command)
{
    char* path;
    int   status;

    path = malloc(PATH_MAX);
    if (path == NULL) {
        return NULL;
    }
    
    sprintf(path, "/chef/bin/%s", command->name);
    return path;
}

static const char* __get_command_path(struct served_application* application, struct served_command* command)
{
    char* path;
    int   status;

    path = malloc(PATH_MAX);
    if (path == NULL) {
        return NULL;
    }
    
    sprintf(path, "/run/chef/%s/%s", application->name, command->path);
    return path;
}

static int __create_application_symlinks(struct served_application* application)
{
    const char* mountRoot = served_application_get_mount_path(application);
    if (mountRoot == NULL) {
        return -1;
    }

    for (int i = 0; i < application->commands_count; i++) {
        const char* symlinkPath = __get_command_symlink_path(&application->commands[i]);
        const char* cmdPath = __get_command_path(application, &application->commands[i]);
        int         status;

        if (symlinkPath == NULL || cmdPath == NULL) {
            free((void*)symlinkPath);
            free((void*)cmdPath);
            // log and continue
            continue;
        }

        // create a link from /chef/bin/<command> => /run/chef/<application>/<cmd-path>
        status = platform_symlink(cmdPath, symlinkPath, 0);
        free((void*)symlinkPath);
        free((void*)cmdPath);

        if (status != 0) {
            // log and continue
        }
    }
    free((void*)mountRoot);
    return 0;
}

static void __remove_application_symlinks(struct served_application* application)
{
    const char* mountRoot = served_application_get_mount_path(application);
    if (mountRoot == NULL) {
        // log
        return;
    }
    
    for (int i = 0; i < application->commands_count; i++) {
        const char* symlinkPath = __get_command_symlink_path(&application->commands[i]);
        int         status      = unlink(symlinkPath);
        free((void*)symlinkPath);
        if (status != 0) {
            // log and continue
        }
    }
    free((void*)mountRoot);
}

int served_application_mount(struct served_application* application)
{
    if (application == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (application->mount == NULL) {
        const char* mountRoot = served_application_get_mount_path(application);
        const char* packPath  = served_application_get_pack_path(application);
        int         status;
        if (mountRoot == NULL || packPath == NULL) {
            free((void*)mountRoot);
            free((void*)packPath);
            return -1;
        }
        
        status = served_mount(packPath, mountRoot, &application->mount);
        free((void*)mountRoot);
        free((void*)packPath);
        if (status != 0) {
            return status;
        }
        return __create_application_symlinks(application);
    }
    return 0;
}

int served_application_unmount(struct served_application* application)
{
    if (application == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (application->mount != NULL) {
        __remove_application_symlinks(application);
        served_unmount(application->mount);
        application->mount = NULL;
    }
    return 0;
}
