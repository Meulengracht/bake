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
#define _GNU_SOURCE // needed for mknod

#include <chef/containerv.h>
#include <chef/containerv/bpf-manager.h>
#include <chef/containerv/layers.h>
#include <chef/containerv/policy.h>
#include <chef/dirs.h>
#include <chef/platform.h>
#include <chef/environment.h>
#include <errno.h>
#include <server.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <vlog.h>

#include "../private.h"

struct __container {
    struct list_item                 item_header;
    char*                            id;
    struct containerv_container*     handle;
    struct containerv_layer_context* layer_context;  // Layer composition context
};

static struct __container* __container_new(struct containerv_container* handle, struct containerv_layer_context* layerContext)
{
    struct __container* container = calloc(1, sizeof(struct __container));
    if (container == NULL) {
        return NULL;
    }
    container->id = platform_strdup(containerv_id(handle));
    if (container->id == NULL) {
        free(container);
        return NULL;
    }
    container->handle = handle;
    container->layer_context = layerContext;
    return container;
}

static void __container_delete(struct __container* container)
{
    int status;

    if (container == NULL) {
        return;
    }

    free(container->id);
    free(container);
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

static enum containerv_mount_flags __to_cv_mount_flags(enum chef_mount_options opts)
{
    enum containerv_mount_flags flags = CV_MOUNT_BIND | CV_MOUNT_CREATE;
    if (opts & CHEF_MOUNT_OPTIONS_READONLY) {
        flags |= CV_MOUNT_READONLY;
    }
    return flags;
}

static enum containerv_layer_type __to_cv_layer_type(enum chef_layer_type type)
{
    switch (type) {
        case CHEF_LAYER_TYPE_BASE_ROOTFS:
            return CONTAINERV_LAYER_BASE_ROOTFS;
        case CHEF_LAYER_TYPE_VAFS_PACKAGE:
            return CONTAINERV_LAYER_VAFS_PACKAGE;
        case CHEF_LAYER_TYPE_HOST_DIRECTORY:
            return CONTAINERV_LAYER_HOST_DIRECTORY;
        case CHEF_LAYER_TYPE_OVERLAY:
            return CONTAINERV_LAYER_OVERLAY;
        default:
            return CONTAINERV_LAYER_BASE_ROOTFS;
    }
}

static struct containerv_layer* __to_cv_layers(struct chef_layer_descriptor* protoLayers, uint32_t count)
{
    struct containerv_layer* cvLayers;
    
    if (protoLayers == NULL || count == 0) {
        return NULL;
    }
    
    cvLayers = calloc(count, sizeof(struct containerv_layer));
    if (cvLayers == NULL) {
        return NULL;
    }
    
    for (uint32_t i = 0; i < count; i++) {
        cvLayers[i].type = __to_cv_layer_type(protoLayers[i].type);
        cvLayers[i].source = protoLayers[i].source;
        cvLayers[i].target = protoLayers[i].target;
        cvLayers[i].readonly = (protoLayers[i].options & CHEF_MOUNT_OPTIONS_READONLY) ? 1 : 0;
    }
    
    return cvLayers;
}

enum chef_status cvd_create(const struct chef_create_parameters* params, const char** id)
{
    struct containerv_options*       opts;
    struct containerv_container*     cvContainer;
    struct __container*              _container;
    struct containerv_layer_context* layerContext = NULL;
    struct containerv_policy*        policy;
    struct containerv_layer*         cvLayers = NULL;
    const char*                      cvdID;
    char                             cvdIDBuffer[17];
    int                              status;
    VLOG_DEBUG("cvd", "cvd_create()\n");

    if (params->layers_count == 0) {
        VLOG_ERROR("cvd", "cvd_create: no layers specified\n");
        return CHEF_STATUS_INVALID_MOUNTS;
    }

    if (params->id == NULL || strlen(params->id) == 0) {
        platform_secure_random_string(&cvdIDBuffer[0], sizeof(cvdIDBuffer) - 1);
        cvdIDBuffer[sizeof(cvdIDBuffer) - 1] = '\0';
        cvdID = &cvdIDBuffer[0];
        
        VLOG_TRACE("cvd", "cvd_create: generated container ID %s\n", cvdID);
    } else {
        cvdID = params->id;
    }

    opts = containerv_options_new();
    if (opts == NULL) {
        VLOG_ERROR("cvd", "failed to allocate memory for container options\n");
        return __chef_status_from_errno();
    }

    VLOG_DEBUG("cvd", "cvd_create: using layer-based approach with %d layers\n", params->layers_count);
    cvLayers = __to_cv_layers(params->layers, params->layers_count);
    if (cvLayers == NULL) {
        VLOG_ERROR("cvd", "cvd_create: failed to convert layers\n");
        containerv_options_delete(opts);
        return CHEF_STATUS_INTERNAL_ERROR;
    }

    // Compose layers into final rootfs
    status = containerv_layers_compose(cvLayers, (int)params->layers_count, cvdID, &layerContext);
    free(cvLayers);
    if (status != 0) {
        VLOG_ERROR("cvd", "cvd_create: failed to compose layers\n");
        containerv_options_delete(opts);
        return CHEF_STATUS_FAILED_ROOTFS_SETUP;
    }

    containerv_options_set_layers(opts, layerContext);

    // setup policy
    policy = containerv_policy_from_strings(params->policy.profiles);
    if (policy != NULL) {
        VLOG_DEBUG("cvd", "cvd_create: applying security policy profiles: %s\n", params->policy.profiles);
        containterv_options_set_policy(opts, policy);
    }

    // setup other config
    containerv_options_set_caps(opts, 
        CV_CAP_FILESYSTEM |
        CV_CAP_PROCESS_CONTROL |
        CV_CAP_IPC
    );

    // create the container
    status = containerv_create(cvdID, opts, &cvContainer);
    containerv_options_delete(opts);
    if (status) {
        if (layerContext) {
            containerv_layers_destroy(layerContext);
        }
        VLOG_ERROR("cvd", "failed to start the container\n");
        return __chef_status_from_errno();
    }

    _container = __container_new(cvContainer, layerContext);
    if (_container == NULL) {
        if (layerContext) {
            containerv_layers_destroy(layerContext);
        }
        VLOG_ERROR("cvd", "failed to allocate memory for the container structure\n");
        __container_delete(_container);
        return __chef_status_from_errno();
    }

    // Store the layer context for cleanup later
    _container->layer_context = layerContext;
    
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
    VLOG_DEBUG("cvd", "cvd_spawn(id=%s, cmd=%s)\n", params->container_id, params->command);

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

    VLOG_DEBUG("cvd", "cvd_spawn: command %s\n", command);
    VLOG_DEBUG("cvd", "cvd_spawn: args: %s\n", arguments);

    // flatten the environment
    if (params->environment_count != 0) {
        VLOG_DEBUG("cvd", "cvd_spawn: parsing environment %u\n", params->environment_count);
        environment = environment_unflatten((const char*)params->environment);
        if (environment == NULL) {
            VLOG_ERROR("cvd", "cvd_spawn: failed to parse provided environment");
            ret = __chef_status_from_errno();
            goto cleanup;
        }
    }

    VLOG_DEBUG("cvd", "cvd_spawn: spawning command\n");
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
    VLOG_DEBUG("cvd", "cvd_kill(id=%s, pid=%u)\n", containerID, pID);

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

    VLOG_DEBUG("cvd", "cvd_transfer(id=%s, direction=%i)\n", params->container_id, direction);

    // find container
    container = __find_container(params->container_id);
    if (container == NULL) {
        VLOG_ERROR("cvd", "cvd_transfer: failed to find container %s", params->container_id);
        return CHEF_STATUS_INVALID_CONTAINER_ID;
    }

    switch (direction) {
        case CVD_TRANSFER_UPLOAD: {
            VLOG_DEBUG("cvd", "cvd_transfer: uploading %s to %s\n", params->source_path, params->destination_path);
            int status = containerv_upload(container->handle, srcs, dsts, 1);
            if (status) {
                return __chef_status_from_errno();
            }
        } break;
        case CVD_TRANSFER_DOWNLOAD: {
            VLOG_DEBUG("cvd", "cvd_transfer: downloading %s to %s\n", params->source_path, params->destination_path);
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
    VLOG_DEBUG("cvd", "cvd_destroy(id=%s)\n", containerID);

    // find container
    container = __find_container(containerID);
    if (container == NULL) {
        VLOG_ERROR("cvd", "cvd_destroy: failed to find container %s", containerID);
        return CHEF_STATUS_INVALID_CONTAINER_ID;
    }

    // Remove from list first
    list_remove(&g_server.containers, &container->item_header);

    // Clean up BPF policy entries for this container
    if (containerv_bpf_manager_is_available()) {
        // Get metrics before cleanup
        struct containerv_bpf_container_metrics c_metrics;
        int metrics_retrieved = (containerv_bpf_manager_get_container_metrics(containerID, &c_metrics) == 0);
        
        VLOG_DEBUG("cvd", "cvd_destroy: cleaning up BPF policy for container %s\n", containerID);
        int bpf_status = containerv_bpf_manager_cleanup_policy(containerID);
        if (bpf_status < 0) {
            VLOG_WARNING("cvd", "cvd_destroy: failed to cleanup BPF policy for %s\n", containerID);
        } else if (metrics_retrieved) {
            VLOG_DEBUG("cvd", "cvd_destroy: BPF policy cleaned up for %s - entries deleted: %d\n",
                      containerID, c_metrics.policy_entry_count);
        }
        
        // Log overall metrics after cleanup
        struct containerv_bpf_metrics metrics;
        if (containerv_bpf_manager_get_metrics(&metrics) == 0) {
            VLOG_TRACE("cvd", "cvd_destroy: BPF metrics - containers: %d, entries: %d/%d, ops: %llu/%llu\n",
                      metrics.total_containers, metrics.total_policy_entries, metrics.max_map_capacity,
                      metrics.total_populate_ops, metrics.total_cleanup_ops);
        }
    }

    status = containerv_destroy(container->handle);
    if (status) {
        VLOG_ERROR("cvd", "cvd_destroy: failed to destroy container %s\n", containerID);
        // Continue with cleanup even if destroy fails
    }

    // Clean up layer context
    if (container->layer_context != NULL) {
        containerv_layers_destroy(container->layer_context);
    }

    __container_delete(container);
    return status == 0 ? CHEF_STATUS_SUCCESS : __chef_status_from_errno();
}
