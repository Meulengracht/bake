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
#include <errno.h>
#include <chef/platform.h>
#include <startup.h>
#include <state.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static const char* g_profileScriptPath = "/etc/profile.d/chef.sh";
static const char* g_profileScript = 
"#!/bin/sh\n"
"export CHEF_HOME=/chef\n"
"export PATH=$PATH:$CHEF_HOME/bin\n";

static int __write_profile_d_script(void)
{
    int status;
    FILE* file;

    // if file exists, then we do not touch it
    file = fopen(g_profileScriptPath, "r");
    if (file != NULL) {
        fclose(file);
        return 0;
    }
    
    file = fopen(g_profileScriptPath, "w");
    if (file == NULL) {
        if (errno == EEXIST) {
            return 0;
        }
        return -1;
    }
    
    status = fwrite(g_profileScript, strlen(g_profileScript), 1, file);
    if (status != 1) {
        fclose(file);
        return -1;
    }

    // change permissions to executable
    status = chmod(g_profileScriptPath, 0755);
    fclose(file);
    return status;
}

static int __ensure_chef_paths(void)
{
    // ensure following paths are created
    // /chef
    // /chef/bin
    // /run/chef
    // /usr/share/chef

    if (platform_mkdir("/chef") != 0) {
        // log
        return -1;
    }

    if (platform_mkdir("/chef/bin") != 0) {
        // log
        return -1;
    }

    if (platform_mkdir("/run/chef") != 0) {
        // log
        return -1;
    }

    if (platform_mkdir("/usr/share/chef") != 0) {
        // log
        return -1;
    }
    return 0;
}

int served_startup(void)
{
    struct served_application** applications;
    int                         applicationCount;
    int                         status;

    status = __write_profile_d_script();
    if (status != 0) {
        // log
        return status;
    }

    status = __ensure_chef_paths();
    if (status != 0) {
        // log
        return status;
    }

    status = served_state_load();
    if (status != 0) {
        // log
        return status;
    }

    status = served_state_get_applications(&applications, &applicationCount);
    if (status != 0) {
        // log
        return status;
    }

    for (int i = 0; i < applicationCount; i++) {
        status = served_application_ensure_paths(applications[i]);
        if (status != 0) {
            // log
            continue;
        }

        status = served_application_mount(applications[i]);
        if (status != 0) {
            // log
            continue;
        }

        status = served_application_start_daemons(applications[i]);
        if (status != 0) {
            // log
        }
    }

    return 0;
}
