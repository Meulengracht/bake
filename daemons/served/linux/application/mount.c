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
#include <chef/package.h>
#include <linux/limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <utils.h>
#include <vlog.h>

static int __create_application_symlinks(struct served_application* application)
{
    const char* mountRoot = served_application_get_mount_path(application);
    if (mountRoot == NULL) {
        return -1;
    }

    for (int i = 0; i < application->commands_count; i++) {
        struct served_command* command = &application->commands[i];
        const char*            symlinkPath;
        const char*            dataPath;
        int                    status;

        symlinkPath = served_application_get_command_symlink_path(application, command);
        dataPath    = served_application_get_data_path(application);
        if (symlinkPath == NULL || dataPath == NULL) {
            free((void*)symlinkPath);
            free((void*)dataPath);
            VLOG_WARNING("mount", "failed to allocate paths for command %s in app %s",
                command->name, application->name);
            continue;
        }

        // create a link from /chef/bin/<command> => ${CHEF_INSTALL_DIR}/lib/chef/serve-exec
        status = platform_symlink(symlinkPath, CHEF_INSTALL_DIR "/lib/chef/serve-exec", 0);
        if (status != 0) {
            free((void*)symlinkPath);
            free((void*)dataPath);
            VLOG_WARNING("mount", "failed to create symlink for command %s in app %s",
                command->name, application->name);
            continue;
        }

        // store the command mount path which is read by serve-exec
        command->symlink = symlinkPath;
        command->data    = dataPath;
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
        int status = platform_unlink(application->commands[i].symlink);

        // then we free resources and NULL them so we are ready to remount
        free((char*)application->commands[i].symlink);
        free((char*)application->commands[i].data);
        application->commands[i].symlink = NULL;
        application->commands[i].data    = NULL;

        // and then we handle the error code, and by handling we mean just
        // log it, because we will ignore any issues encountered in this loop
        if (status != 0) {
            VLOG_WARNING("mount", "failed to remove symlink for command %s in app %s",
                application->commands[i].name, application->name);
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
