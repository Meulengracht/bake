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
#include <vlog.h>

static const char* g_profileScriptPath = "/etc/profile.d/chef.sh";
static const char* g_profileScript = 
"#!/bin/sh\n"
"export CHEF_HOME=/chef\n"
"export PATH=$PATH:$CHEF_HOME/bin\n";

static int __write_profile_d_script(void)
{
    int    status;
    FILE*  file;
    size_t written;
    VLOG_TRACE("startup", "__write_profile_d_script()\n");

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
    
    written = fwrite(g_profileScript, strlen(g_profileScript), 1, file);
    if (written != 1) {
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
    VLOG_TRACE("startup", "__ensure_chef_paths()\n");
    // ensure following paths are created
    // /chef
    // /chef/bin
    // /run/chef
    // /usr/share/chef
    // /var/chef
    // /var/chef/packs

    if (platform_mkdir("/chef") != 0) {
        VLOG_ERROR("startup", "failed to create path /chef\n");
        return -1;
    }

    if (platform_mkdir("/chef/bin") != 0) {
        VLOG_ERROR("startup", "failed to create path /chef/bin\n");
        return -1;
    }

    if (platform_mkdir("/run/chef") != 0) {
        VLOG_ERROR("startup", "failed to create path /run/chef\n");
        return -1;
    }

    if (platform_mkdir("/usr/share/chef") != 0) {
        VLOG_ERROR("startup", "failed to create path /usr/share/chef\n");
        return -1;
    }

    if (platform_mkdir("/var/chef") != 0) {
        VLOG_ERROR("startup", "failed to create path /var/chef\n");
        return -1;
    }
    return 0;
}

int served_startup(void)
{
    struct served_application** applications;
    int                         applicationCount;
    int                         status;
    VLOG_TRACE("startup", "served_startup()\n");

    status = __write_profile_d_script();
    if (status != 0) {
        VLOG_ERROR("startup", "failed to write profile script to path %s\n", g_profileScriptPath);
        return status;
    }

    status = __ensure_chef_paths();
    if (status != 0) {
        VLOG_ERROR("startup", "failed to write necessary chef paths\n");
        return status;
    }

    status = served_state_load();
    if (status != 0) {
        VLOG_ERROR("startup", "failed to load/initialize state\n");
        return status;
    }

    status = served_state_get_applications(&applications, &applicationCount);
    if (status != 0) {
        VLOG_ERROR("startup", "failed to load applications from state\n");
        return status;
    }

    VLOG_DEBUG("startup", "initializing %i applications\n", applicationCount);
    for (int i = 0; i < applicationCount; i++) {
        status = served_application_ensure_paths(applications[i]);
        if (status != 0) {
            VLOG_WARNING("startup", "failed to create application paths for %s\n", applications[i]->name);
            continue;
        }

        status = served_application_mount(applications[i]);
        if (status != 0) {
            VLOG_WARNING("startup", "failed to mount application %s\n", applications[i]->name);
            continue;
        }

        status = served_application_start_daemons(applications[i]);
        if (status != 0) {
            VLOG_WARNING("startup", "failed to start daemons for application %s\n", applications[i]->name);
        }
    }

    VLOG_TRACE("startup", "complete\n");
    return 0;
}
