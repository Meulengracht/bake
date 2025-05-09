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

#include <chef/containerv.h>
#include <chef/platform.h>
#include <chef/environment.h>
#include <errno.h>
#include <server.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <vlog.h>

struct __container {
    struct list_item             item_header;
    char*                        id;
    struct containerv_container* handle;
};


static struct __container* __container_new(const char* rootfs, struct containerv_container* handle)
{
    struct __container* container = calloc(1, sizeof(struct __container));
    if (container == NULL) {
        return NULL;
    }
    container->id = platform_strdup(strrchr(rootfs, '/') + 1);
    if (container->id == NULL) {
        free(container);
        return NULL;
    }
    container->handle = handle;
    return container;
}

static struct {
    struct list containers;
} g_server = { 0 };

static enum chef_status __chef_status_from_errno(void) {
    switch (errno) {

        default:
            return CHEF_STATUS_INTERNAL_ERROR;
    }
}

static int __generate_container_directory(char** pathOut)
{
    char* template = "/run/chef/cvd/XXXXXX";
    if (mkdtemp(&template[0]) == NULL) {
        VLOG_ERROR("cvd", "failed to get a temporary filename for log %s\n", template);
        return -1;
    }
    *pathOut = platform_strdup(template);
    return 0;
}

static int __resolve_rootfs(const struct chef_create_parameters* params, char** pathOut)
{
    char* path = NULL;
    int   status;
    VLOG_TRACE("cvd", "__resolve_rootfs(rootfs=%s, type=%i)\n", params->rootfs, params->type);

    // generate a directory for the container in any case, this is where
    // stuff will be created / mounted for mount ns.
    // this will also give us an id
    status = __generate_container_directory(&path);
    if (status) {
        VLOG_ERROR("cvd", "failed to generate a container directory\n");
        return status;
    }

    switch (params->type) {
        // Create a new rootfs using debootstrap from the host
        case CHEF_ROOTFS_TYPE_DEBOOTSTRAP:
        {
            status = cvd_rootfs_setup_debootstrap(path);
            if (status) {
                VLOG_ERROR("cvd", "failed to setup debootstrap container\n");
                platform_rmdir(path);
                free(path);
                return status;
            }

            // Set the rootfs path
            *pathOut = path;
            return 0;
        }
        case CHEF_ROOTFS_TYPE_OSBASE:
        case CHEF_ROOTFS_TYPE_IMAGE:
            VLOG_WARNING("cvd", "__resolve_rootfs: ROOTFS TYPE NOT IMPLEMENTED\n");
            break;
    }
}

static enum containerv_mount_flags __to_cv_mount_flags(enum chef_mount_options opts)
{
    enum containerv_mount_flags flags = CV_MOUNT_BIND;
    if (opts & CHEF_MOUNT_OPTIONS_READONLY) {
        flags |= CV_MOUNT_READONLY;
    }
    return flags;
}

static int __resolve_mounts(struct containerv_options* opts, const char* rootfs, struct chef_container_mount* mounts, size_t count)
{
    struct containerv_mount* cv_mounts;
    VLOG_TRACE("cvd", "__resolve_mounts(rootfs=%s, count=%zu)\n", rootfs, count);

    // early exit
    if (count == 0) {
        return 0;
    }

    cv_mounts = calloc(count, sizeof(struct containerv_mount));
    if (cv_mounts == NULL) {
        VLOG_ERROR("cvd", "__resolve_mounts: failed to allocate memory for mounts\n");
        return -1;
    }

    for (size_t i = 0; i < count; i++) {
        VLOG_TRACE("cvd", "__resolve_mounts: %zu - %s => %s\n", i, mounts[i].host_path, mounts[i].container_path);
        cv_mounts[i].what = mounts[i].host_path;
        cv_mounts[i].where = mounts[i].container_path;
        cv_mounts[i].flags = __to_cv_mount_flags(mounts[i].options);
    }

    containerv_options_set_mounts(opts, cv_mounts, (int)count);
    return 0;
}

enum chef_status cvd_create(const struct chef_create_parameters* params, const char** id)
{
    struct containerv_options*   opts;
    struct containerv_container* cv_container;
    struct __container*          _container;
    char*                        rootfs;
    int                          status;
    VLOG_TRACE("cvd", "cvd_create()\n");

    opts = containerv_options_new();
    if (opts == NULL) {
        VLOG_ERROR("cvd", "failed to allocate memory for container options\n");
        return __chef_status_from_errno();
    }

    // resolve the type of roots
    status = __resolve_rootfs(params, &rootfs);
    if (status) {
        VLOG_ERROR("cvd", "failed to resolve rootfs\n");
        return CHEF_STATUS_FAILED_ROOTFS_SETUP;
    }

    // setup mounts
    status = __resolve_mounts(opts, rootfs, params->mounts, params->mounts_count);
    if (status) {
        containerv_options_delete(opts);
        VLOG_ERROR("cvd", "failed to resolve rootfs mounts\n");
        return CHEF_STATUS_INVALID_MOUNTS;
    }

    // setup other config
    containerv_options_set_caps(opts, 
        CV_CAP_FILESYSTEM |
        CV_CAP_PROCESS_CONTROL |
        CV_CAP_IPC
    );

    // create the container
    status = containerv_create(rootfs, opts, &cv_container);
    containerv_options_delete(opts);
    if (status) {
        VLOG_ERROR("cvd", "failed to start the container\n");
        return __chef_status_from_errno();
    }

    _container =  __container_new(rootfs, cv_container);
    if (_container == NULL) {
        VLOG_ERROR("cvd", "failed to allocate memory for the container structure\n");
        return __chef_status_from_errno();
    }
    list_add(&g_server.containers, &_container->item_header);
    *id = _container->id;
    return CHEF_STATUS_SUCCESS;
}

static int __split_command(const char* line, char** command, char** arguments)
{
    char  buffer[1024] = { 0 };
    char* p = strchr(line, ' ');

    if (p != NULL) {
        strncpy(&buffer[0], line, p - line);
        *command = platform_strdup(&buffer[0]);
        *arguments = platform_strdup(p + 1);
        if (*command == NULL || *arguments == NULL) {
            free(*command);
            return -1;
        }
    } else {
        *command = platform_strdup(line);
        if (*command == NULL) {
            return -1;
        }
        *arguments = NULL;
    }
    return 0;
}

static enum container_spawn_flags __convert_to_spawn_flags(enum chef_spawn_options options)
{
    enum container_spawn_flags flags = 0;
    if (options & CHEF_SPAWN_OPTIONS_WAIT) {
        flags |= CV_SPAWN_WAIT;
    }
    return flags;
}

static struct __container* __find_container(const char* id)
{
    struct list_item* i;

    list_foreach(&g_server.containers, i) {
        struct __container* container = (struct __container*)i;
        if (strcmp(container->id, id) == 0) {
            return container;
        }
    }

    errno = ENOENT;
    return NULL;
}

enum chef_status cvd_spawn(const struct chef_spawn_parameters* params, unsigned int* pIDOut)
{
    struct __container*             container;
    struct containerv_spawn_options opts;
    int                             status;
    char*                           command = NULL;
    char*                           arguments = NULL;
    char**                          environment = NULL;
    enum chef_status                ret = CHEF_STATUS_SUCCESS;
    VLOG_TRACE("cvd", "cvd_spawn(id=%s, cmd=%s)\n", params->container_id, params->command);

    // find container
    container = __find_container(params->container_id);
    if (container == NULL) {
        VLOG_ERROR("cvd", "cvd_spawn: failed to find container %s", params->container_id);
        return CHEF_STATUS_INVALID_CONTAINER_ID;
    }

    // split up command into command and arguments
    status = __split_command(params->command, &command, &arguments);
    if (status) {
        VLOG_ERROR("cvd", "cvd_spawn: failed to split command %s", params->command);
        return __chef_status_from_errno();
    }

    // flatten the environment
    if (params->environment_count != 0) {
        environment = environment_unflatten((const char*)params->environment);
        if (environment == NULL) {
            VLOG_ERROR("cvd", "cvd_spawn: failed to parse provided environment");
            ret = __chef_status_from_errno();
            goto cleanup;
        }
    }

    status = containerv_spawn(
        container->handle,
        command,
        &(struct containerv_spawn_options) {
            .arguments = arguments,
            .environment = (const char* const*)environment,
            .flags = __convert_to_spawn_flags(params->options)
        },
        pIDOut
    );
    if (status) {
        VLOG_ERROR("cvd", "cvd_spawn: failed to execute %s\n", command);
        ret = __chef_status_from_errno();
    }

cleanup:
    environment_destroy(environment);
    free(command);
    free(arguments);
    return ret;
}

enum chef_status cvd_kill(const char* containerID, const unsigned int pID)
{
    struct __container* container;
    int                 status;
    VLOG_TRACE("cvd", "cvd_kill(id=%s, pid=%u)\n", containerID, pID);

    // find container
    container = __find_container(containerID);
    if (container == NULL) {
        VLOG_ERROR("cvd", "cvd_kill: failed to find container %s", containerID);
        return CHEF_STATUS_INVALID_CONTAINER_ID;
    }

    status = containerv_kill(container->handle, pID);
    if (status) {
        VLOG_ERROR("cvd", "cvd_kill: failed to kill process %u\n", pID);
        return __chef_status_from_errno();
    }
    return CHEF_STATUS_SUCCESS;
}

enum chef_status cvd_transfer(const struct chef_file_parameters* params, enum cvd_transfer_direction direction)
{
    struct __container* container;
    const char* srcs[] = { params->source_path, NULL };
    const char* dsts[] = { params->destination_path, NULL };

    VLOG_TRACE("cvd", "cvd_transfer(id=%s, direction=%i)\n", params->container_id, direction);

    // find container
    container = __find_container(params->container_id);
    if (container == NULL) {
        VLOG_ERROR("cvd", "cvd_transfer: failed to find container %s", params->container_id);
        return CHEF_STATUS_INVALID_CONTAINER_ID;
    }

    switch (direction) {
        case CVD_TRANSFER_UPLOAD: {
            VLOG_TRACE("cvd", "cvd_transfer: uploading %s to %s\n", params->source_path, params->destination_path);
            int status = containerv_upload(container->handle, srcs, dsts, 1);
            if (status) {
                return __chef_status_from_errno();
            }
        } break;
        case CVD_TRANSFER_DOWNLOAD: {
            VLOG_TRACE("cvd", "cvd_transfer: downloading %s to %s\n", params->source_path, params->destination_path);
            int status = containerv_download(container->handle, srcs, dsts, 1);
            if (status) {
                return __chef_status_from_errno();
            }

            // switch user of the file
        } break;
        default:
            return CHEF_STATUS_INTERNAL_ERROR;
    }
    return CHEF_STATUS_SUCCESS;
}

enum chef_status cvd_destroy(const char* containerID)
{
    struct __container* container;
    int                 status;
    VLOG_TRACE("cvd", "cvd_destroy(id=%s)\n", containerID);

    // find container
    container = __find_container(containerID);
    if (container == NULL) {
        VLOG_ERROR("cvd", "cvd_destroy: failed to find container %s", containerID);
        return CHEF_STATUS_INVALID_CONTAINER_ID;
    }

    status = containerv_destroy(container->handle);
    if (status) {
        VLOG_ERROR("cvd", "cvd_destroy: failed to destroy container %s\n", containerID);
        return __chef_status_from_errno();
    }
    return CHEF_STATUS_SUCCESS;
}
