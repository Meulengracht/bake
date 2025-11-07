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

#include <errno.h>
#include <chef/platform.h>
#include <startup.h>
#include <state.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <utils.h>
#include <vlog.h>

#include <transaction/sets.h>
#include <transaction/transaction.h>

#include <chef/client.h>
#include <chef/api/package.h>

static const char* g_profileScriptPath = "/etc/profile.d/chef.sh";
static const char* g_profileScript = 
"#!/bin/sh\n"
"export CHEF_HOME=/chef\n"
"export PATH=$CHEF_HOME/bin:$PATH\n";

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
    char* path;

    VLOG_TRACE("startup", "__ensure_chef_paths()\n");
    
    // ensure following paths are created
    // /chef
    // /chef/bin
    // /var/chef
    // /var/chef/mnt
    // /var/chef/packs

    path = served_paths_path("/chef/bin");
    if (platform_mkdir(path) != 0) {
        VLOG_ERROR("startup", "failed to create path %s\n", path);
        free(path);
        return -1;
    }
    free(path);

    path = served_paths_path("/var/chef/packs");
    if (platform_mkdir(path) != 0) {
        VLOG_ERROR("startup", "failed to create path %s\n", path);
        free(path);
        return -1;
    }
    free(path);

    path = served_paths_path("/var/chef/mnt");
    if (platform_mkdir(path) != 0) {
        VLOG_ERROR("startup", "failed to create path %s\n", path);
        free(path);
        return -1;
    }
    free(path);
    return 0;
}

static int __resolve_package(const char* publisher, const char* package, const char* platform, const char* arch, const char* channel, struct chef_version* version, const char* path, int* revisionDownloaded)
{
    struct chef_download_params downloadParams;
    int                         status;
    VLOG_DEBUG("served", "__resolve_package()\n");

    // initialize download params
    downloadParams.publisher = publisher;
    downloadParams.package   = package;
    downloadParams.platform  = platform;
    downloadParams.arch      = arch;
    downloadParams.channel   = channel;
    downloadParams.revision  = 0;

    VLOG_TRACE("served", "downloading package %s/%s\n", publisher, package),
    status = chefclient_pack_download(&downloadParams, path);
    if (status == 0) {
        *revisionDownloaded = downloadParams.revision;
    }
    return status;
}

int served_resolver_initialize(void)
{
    int status;
    VLOG_DEBUG("cookd", "served_resolver_initialize()\n");

    status = chefclient_initialize();
    if (status != 0) {
        VLOG_ERROR("cookd", "failed to initialize chef client\n");
        return -1;
    }

    status = store_initialize(&(struct store_parameters) {
        .platform = CHEF_PLATFORM_STR,
        .architecture = CHEF_ARCHITECTURE_STR,
        .backend = {
            .resolve_package = __resolve_package
        }
    });
    if (status) {
        VLOG_ERROR("cookd", "failed to initialize store\n");
        chefclient_cleanup();
    }
    return status;
}

int served_startup(void)
{
    unsigned int transactionId;
    int          status;
    VLOG_TRACE("startup", "served_startup()\n");

#ifndef CHEF_AS_SNAP
    status = __write_profile_d_script();
    if (status != 0) {
        VLOG_ERROR("startup", "failed to write profile script to path %s\n", g_profileScriptPath);
        return status;
    }
#endif

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

    VLOG_TRACE("startup", "initiating startup transaction\n");
    transactionId = served_transaction_create(&(struct served_transaction_options) {
        .name = "system-startup",
        .description = "Served system initialization",
        .type = SERVED_TRANSACTION_TYPE_EPHEMERAL,
        .stateSet = g_stateSetStartup
    });
    if (transactionId == (unsigned int)-1) {
        VLOG_ERROR("startup", "failed to create startup transaction\n");
        return -1;
    }

    VLOG_TRACE("startup", "startup-transaction: %u\n", transactionId);
    return 0;
}
