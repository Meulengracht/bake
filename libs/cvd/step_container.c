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

#include <chef/cvd.h>
#include <chef/list.h>
#include <chef/dirs.h>
#include <chef/platform.h>
#include <errno.h>
#include <stdlib.h>
#include <vlog.h>

const char* g_possibleBakeCtlPaths[] = {
    // relative path from the executable
    "../libexec/chef/bakectl",
    // when running from the daemon, bakectl is adjacent
    "bakectl",
    // from build folder
    "../../bin/bakectl",
    // fallbacks if wtf?
    "/usr/libexec/chef/bakectl",
    "/usr/local/libexec/chef/bakectl",
    NULL
};

static int __find_bakectl(char** resolvedOut)
{
    char   buffer[PATH_MAX] = { 0 };
    char   dirnm[PATH_MAX] = { 0 };
    char*  resolved = NULL;
    char*  p;
    int    status;
    size_t index;
    VLOG_DEBUG("bake", "__find_bakectl()\n");

    status = readlink("/proc/self/exe", &buffer[0], PATH_MAX);
    if (status < 0) {
        VLOG_ERROR("bake", "__install_bakectl: failed to read /proc/self/exe\n");
        return status;
    }

    p = strrchr(&buffer[0], CHEF_PATH_SEPARATOR);
    if (p == NULL) {
        VLOG_ERROR("bake", "__install_bakectl: could not find separator in %s\n", &buffer[0]);
        return -1;
    }

    index = (p + 1) - (&buffer[0]);
    strncpy(&dirnm[0], &buffer[0], index);

    VLOG_DEBUG("bake", "testing paths from %s\n", &dirnm[0]);
    for (int i = 0; g_possibleBakeCtlPaths[i] != NULL; i++) {
        const char* pathToUse = g_possibleBakeCtlPaths[i];
        if (g_possibleBakeCtlPaths[i][0] != '/') {
            strcpy(&dirnm[index], g_possibleBakeCtlPaths[i]);
            pathToUse = &dirnm[0];
        }
        resolved = realpath(pathToUse, NULL);
        if (resolved != NULL) {
            VLOG_DEBUG("bake", "__install_bakectl: found bakectl here: %s\n", pathToUse);
            break;
        }
        VLOG_DEBUG("bake", "__install_bakectl: tried %s\n", pathToUse);
    }

    if (resolved == NULL) {
        status = readlink("/proc/self/exe", &dirnm[0], PATH_MAX);
        if (status < 0) {
            VLOG_ERROR("bake", "__install_bakectl: failed to read /proc/self/exe\n");
            return status;
        }
        VLOG_ERROR("bake", "__install_bakectl: failed to resolve bakectl from %s\n", &dirnm[0]);
        return -1;
    }
    *resolvedOut = resolved;
    return 0;
}

int bake_build_setup(struct __bake_build_context* bctx)
{
    struct chef_container_mount mounts[3];
    int                         status;
    char*                       bakectlPath;
    unsigned int                pid;
    char                        buffer[1024];
    VLOG_DEBUG("bake", "bake_build_setup()\n");

    if (bctx->cvd_client == NULL) {
        errno = ENOTSUP;
        return -1;
    }

    // project path
    mounts[0].host_path = (char*)bctx->host_cwd;
    mounts[0].container_path = "/chef/project";
    mounts[0].options = CHEF_MOUNT_OPTIONS_READONLY;

    // fridge path
    mounts[1].host_path = (char*)chef_dirs_fridge();
    mounts[1].container_path = "/chef/fridge";
    mounts[1].options = CHEF_MOUNT_OPTIONS_READONLY;

    // fridge path
    mounts[2].host_path = (char*)chef_dirs_store();
    mounts[2].container_path = "/chef/store";
    mounts[2].options = CHEF_MOUNT_OPTIONS_READONLY;

    status = bake_client_create_container(bctx, &mounts[0], 3);
    if (status) {
        VLOG_ERROR("bake", "bake_build_setup: failed to create build container\n");
        return status;
    }

    status = __find_bakectl(&bakectlPath);
    if (status) {
        VLOG_ERROR("bake", "bake_build_setup: failed to locate bakectl for container\n");
        bake_client_destroy_container(bctx);
        return status;
    }
    
    status = bake_client_upload(bctx, bakectlPath, bctx->bakectl_path);
    if (status) {
        VLOG_ERROR("bake", "bake_build_setup: failed to write bakectl in container\n");
        bake_client_destroy_container(bctx);
        return status;
    }
    free(bakectlPath);

    snprintf(&buffer[0], sizeof(buffer),
        "%s init --recipe %s",
        bctx->bakectl_path, bctx->recipe_path
    );

    status = bake_client_spawn(
        bctx,
        &buffer[0],
        CHEF_SPAWN_OPTIONS_WAIT,
        &pid
    );
    if (status) {
        VLOG_ERROR("bake", "failed to setup project inside the container\n");
    }
    return status;
}
