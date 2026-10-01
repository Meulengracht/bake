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
 */

#include <vlog.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "private.h"

/**
 * @brief Configure HyperV shared folder for host bind mount
 * @param container Container to configure
 * @param host_path Host path to share
 * @param container_path Path inside the container/VM
 * @param readonly Read-only flag
 * @return 0 on success, -1 on failure
 */
static int __windows_configure_shared_folder(
    struct containerv_container* container,
    const char* host_path,
    const char* container_path,
    int         readonly)
{
    char  name[64];

    if (container == NULL || host_path == NULL || host_path[0] == '\0' ||
        container_path == NULL || container_path[0] == '\0') {
        return -1;
    }

    VLOG_DEBUG("containerv[windows]", "configuring shared folder: %s (ro=%d)\n",
              host_path, readonly);

    if (__windows_prepare_share_dir(host_path, readonly) != 0) {
        return -1;
    }

    if (!container->guest_is_windows && strcmp(container_path, "/chef/rootfs") == 0) {
        snprintf(name, sizeof(name), "%s", HCS_LCOW_ROOTFS_SHARE_NAME);
    } else {
        __hcs_plan9_share_name(host_path, name, sizeof(name));
    }

    if (container->hcs_system != NULL) {
        if (windows_grant_vm_group_access(host_path) != 0) {
            VLOG_ERROR("containerv[windows]", "failed to grant VM group access to shared folder %s\n", host_path);
            return -1;
        }

        if (!container->guest_is_windows) {
            if (__hcs_plan9_mapped_dir_add(container, name, container_path, readonly) != 0) {
                VLOG_ERROR("containerv[windows]", "failed to mount predeclared Plan9 share %s for %s\n", name, host_path);
                return -1;
            }
            return 0;
        }

        if (__hcs_plan9_share_add(container, name, host_path, container_path, readonly) != 0) {
            VLOG_ERROR("containerv[windows]", "failed to add Plan9 share %s for %s\n", name, host_path);
            return -1;
        }
    }

    return 0;
}

struct __windows_volume_iter_ctx {
    struct containerv_container* container;
    int                          status;
    int                          enable_plan9;
    int                          lcow;
};

static int __windows_layers_hostdir_cb(
    const char* host_path,
    const char* container_path,
    int         readonly,
    void*       user)
{
    struct __windows_volume_iter_ctx* ctx = (struct __windows_volume_iter_ctx*)user;
    char* guest_path = NULL;
    int   rc;

    if (ctx == NULL || ctx->container == NULL) {
        return -1;
    }

    if (!ctx->enable_plan9) {
        return 0;
    }

    if (ctx->lcow) {
        size_t length = strlen("/chef/rootfs") + strlen(container_path) + 1;
        guest_path = calloc(length, 1);
        if (guest_path == NULL) {
            return -1;
        }
        snprintf(guest_path, length, "/chef/rootfs%s", container_path);
    }

    rc = __windows_configure_shared_folder(
        ctx->container,
        host_path,
        guest_path != NULL ? guest_path : container_path,
        readonly);
    free(guest_path);
    if (rc != 0) {
        VLOG_WARNING("containerv[windows]", "failed to share host directory %s (ro=%d)\n",
                     host_path, readonly);
        ctx->status = rc;
        return rc;
    }

    return 0;
}

/**
 * @brief Process and configure volumes for Windows container
 * @param container Container to configure volumes for
 * @param options Container options with mount configuration
 * @return 0 on success, -1 on failure
 */
int __windows_setup_volumes(
    struct containerv_container* container,
    const struct containerv_options* options)
{
    if (!options || !options->layers) {
        VLOG_DEBUG("containerv[windows]", "no layers/volumes to configure\n");
        return 0;
    }

    VLOG_DEBUG("containerv[windows]", "setting up volumes for container %s from layers\n", container->id);

    struct __windows_volume_iter_ctx ctx = {
        .container = container,
        .status = 0,
        .enable_plan9 = 0,
        .lcow = options->windows_container_type == WINDOWS_CONTAINER_TYPE_LINUX,
    };

    if (options->windows_container_type == WINDOWS_CONTAINER_TYPE_LINUX ||
        options->windows_container.isolation == WINDOWS_CONTAINER_ISOLATION_HYPERV) {
        ctx.enable_plan9 = 1;
    }

    if (options->windows_container_type == WINDOWS_CONTAINER_TYPE_LINUX && container->rootfs != NULL) {
        if (__windows_configure_shared_folder(container, container->rootfs, "/chef/rootfs", 0) != 0) {
            VLOG_ERROR("containerv[windows]", "failed to share LCOW rootfs %s\n", container->rootfs);
            return -1;
        }
    }

    // Always share staging directory for Hyper-V containers (Plan9).
    if (ctx.enable_plan9 && container->runtime_dir != NULL) {
        char stage_host[MAX_PATH];
        snprintf(stage_host, sizeof(stage_host), "%s\\staging", container->runtime_dir);
        if (__windows_configure_shared_folder(container, stage_host, "/chef/rootfs/chef/staging", 0) != 0) {
            VLOG_WARNING("containerv[windows]", "failed to share staging directory %s\n", stage_host);
        }
    }

    int status = containerv_layers_iterate(
        options->layers,
        CONTAINERV_LAYER_HOST_DIRECTORY,
        __windows_layers_hostdir_cb,
        &ctx);

    if (status != 0) {
        VLOG_ERROR("containerv[windows]", "failed to configure one or more host-directory layers\n");
        return -1;
    }

    VLOG_DEBUG("containerv[windows]", "volume setup from layers completed\n");
    return 0;
}
