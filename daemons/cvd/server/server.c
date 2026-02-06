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
#include <chef/containerv/layers.h>
#include <chef/containerv/policy.h>
#include <chef/dirs.h>
#include <chef/package.h>
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
    struct list                      processes;
    unsigned int                     next_process_id;
};

struct __container_process {
    struct list_item     item_header;
    unsigned int         public_id;
    process_handle_t     handle;
};

static void __container_process_delete(void* item)
{
    struct __container_process* proc = (struct __container_process*)item;
    if (proc == NULL) {
        return;
    }
    free(proc);
}

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
    list_init(&container->processes);
    container->next_process_id = 0;
    return container;
}

static void __container_delete(struct __container* container)
{
    if (container == NULL) {
        return;
    }

    list_destroy(&container->processes, __container_process_delete);
    free(container->id);
    free(container);
}

static unsigned int __container_register_process(struct __container* container, process_handle_t handle)
{
    struct __container_process* proc;

    if (container == NULL) {
        return 0;
    }

    proc = calloc(1, sizeof(struct __container_process));
    if (proc == NULL) {
        return 0;
    }

    container->next_process_id++;
    if (container->next_process_id == 0) {
        container->next_process_id++;
    }

    proc->public_id = container->next_process_id;
    proc->handle = handle;
    list_add(&container->processes, &proc->item_header);
    return proc->public_id;
}

static struct __container_process* __container_find_process(struct __container* container, unsigned int public_id)
{
    struct list_item* i;

    if (container == NULL || public_id == 0) {
        return NULL;
    }

    list_foreach(&container->processes, i) {
        struct __container_process* proc = (struct __container_process*)i;
        if (proc->public_id == public_id) {
            return proc;
        }
    }

    return NULL;
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

static int __is_nonempty(const char* s)
{
    return (s != NULL && s[0] != '\0');
}

static int __spec_contains_plugin(const struct chef_policy_spec* spec, const char* needle)
{
    for (uint32_t i = 0; i < spec->plugins_count; i++) {
        if (strcmp(spec->plugins[i].name, needle) == 0) {
            return 1;
        }
    }
    return 0;
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

static struct containerv_policy_plugin* __to_policy_plugin(struct chef_policy_plugin* protocolPlugin)
{
    struct containerv_policy_plugin* plugin = calloc(1, sizeof(*plugin));
    if (plugin == NULL) {
        return NULL;
    }
    
    plugin->name = protocolPlugin->name;
    return plugin;
}

static void __free_policy_plugin(void* item)
{
    free(item);
}

struct containerv_policy* __policy_from_spec(const struct chef_policy_spec* spec)
{
    struct list               plugins;
    struct containerv_policy* policy;

    list_init(&plugins);

    // Always include minimal base policy.
    {
        struct containerv_policy_plugin* plugin = calloc(1, sizeof(*plugin));
        if (!plugin) {
            return NULL;
        }
        plugin->name = "minimal";
        list_add(&plugins, &plugin->header);
    }

    if (spec->plugins_count) {
        for (uint32_t i = 0; i < spec->plugins_count; i++) {
            struct containerv_policy_plugin* plugin = 
                __to_policy_plugin(&spec->plugins[i]);
            if (plugin == NULL) {
                list_destroy(&plugins, __free_policy_plugin);
            }
            list_add(&plugins, &plugin->header);
        }
    }

    policy = containerv_policy_new(&plugins);
    list_destroy(&plugins, __free_policy_plugin);
    return policy;
}

struct __create_container_params {
    const char*                      id; // do not cleanup
    struct containerv_options*       opts; // cleaned up
    struct containerv_container*     container; // cleanup on failures
    struct containerv_layer*         layers; // cleanup
    int                              layers_count;
    struct containerv_layer_context* layer_context; // cleanup on failures
};

static void __create_container_params_cleanup(struct __create_container_params* params)
{
    if (params == NULL) {
        return;
    }

    containerv_options_delete(params->opts);
    free(params->layers);
}

#ifdef CHEF_ON_LINUX
static enum chef_status __create_linux_container(const struct chef_create_parameters* params, struct __create_container_params* containerParams)
{
    struct containerv_policy* policy;
    int                       status;

    status = containerv_layers_compose(
        containerParams->layers,
        containerParams->layers_count,
        containerParams->id,
        &containerParams->layer_context
    );
    if (status) {
        VLOG_ERROR("cvd", "cvd_create: failed to compose layers\n");
        return CHEF_STATUS_FAILED_ROOTFS_SETUP;
    }

    containerv_options_set_layers(
        containerParams->opts,
        containerParams->layer_context
    );

    // setup policy
    policy = __policy_from_spec(&params->policy);
    if (policy != NULL) {
        VLOG_DEBUG("cvd", "cvd_create: applying security policy profiles\n");
        containerv_options_set_policy(containerParams->opts, policy);
    }

    // Optional network configuration
    if (__is_nonempty(params->network.container_ip) 
            && __is_nonempty(params->network.container_netmask)) {
        containerv_options_set_network_ex(
            containerParams->opts,
            params->network.container_ip,
            params->network.container_netmask,
            __is_nonempty(params->network.host_ip) ? params->network.host_ip : NULL,
            __is_nonempty(params->network.gateway_ip) ? params->network.gateway_ip : NULL,
            __is_nonempty(params->network.dns) ? params->network.dns : NULL
        );
    }

    // setup other config
    enum containerv_capabilities caps =
        CV_CAP_FILESYSTEM |
        CV_CAP_PROCESS_CONTROL |
        CV_CAP_IPC;

    // Enable network capability if requested by policy profile or network configuration.
    if (__spec_contains_plugin(&params->policy, "network") ||
        (__is_nonempty(params->network.container_ip) 
            && __is_nonempty(params->network.container_netmask))) {
        caps |= CV_CAP_NETWORK;
    }

    containerv_options_set_caps(containerParams->opts, caps);
    
    status = containerv_create(
        containerParams->id,
        containerParams->opts,
        &containerParams->container
    );
    if (status) {
        VLOG_ERROR("cvd", "failed to start the container\n");
        return __chef_status_from_errno();
    }
    return CHEF_STATUS_SUCCESS;
}
#elif CHEF_ON_WINDOWS
static int __windows_hcs_has_disallowed_layers(const struct chef_create_parameters* params)
{
    if (params == NULL || params->layers == NULL || params->layers_count == 0) {
        return 0;
    }

    for (uint32_t i = 0; i < params->layers_count; i++) {
        switch (params->layers[i].type) {
            case CHEF_LAYER_TYPE_OVERLAY:
                return 1;
            default:
                break;
        }
    }

    return 0;
}

static enum chef_status __create_hyperv_container(const struct chef_create_parameters* params, struct __create_container_params* containerParams)
{
    struct containerv_policy* policy;
    char**                    wcow_parent_layers = NULL;
    int                       wcow_parent_layer_count = 0;
    int                       status;
    VLOG_DEBUG("cvd", "__create_hyperv_container()\n");
    
    if (__windows_hcs_has_disallowed_layers(params)) {
        VLOG_ERROR("cvd", "cvd_create: HCS container mode does not support OVERLAY layers on Windows. Remove overlays.\n");
        return CHEF_STATUS_FAILED_ROOTFS_SETUP;
    }

    // WCOW vs LCOW selection for the HCS container backend.
    const int is_lcow = params->gtype == CHEF_GUEST_TYPE_LINUX;
    containerv_options_set_windows_container_type(
        containerParams->opts,
        is_lcow ? CV_WIN_CONTAINER_TYPE_LINUX 
                    : CV_WIN_CONTAINER_TYPE_WINDOWS
    );

    // Default to Hyper-V isolation (true Hyper-V containers).
    containerv_options_set_windows_container_isolation(
        containerParams->opts, 
        CV_WIN_CONTAINER_ISOLATION_HYPERV
    );

    if (is_lcow) {
        // LCOW requires HvRuntime settings for the Linux utility VM.
        // These values are passed through to schema1 HvRuntime.
        const char* uvm_image = params->guest_windows.lcow_uvm_image_path;
        const char* kernel = params->guest_windows.lcow_kernel_file;
        const char* initrd = params->guest_windows.lcow_initrd_file;
        const char* boot = params->guest_windows.lcow_boot_parameters;

        if (uvm_image == NULL || uvm_image[0] == '\0') {
            VLOG_ERROR("cvd", "cvd_create: LCOW requires UVM image path or URL in guest_windows options\n");
            return CHEF_STATUS_FAILED_ROOTFS_SETUP;
        }
        containerv_options_set_windows_lcow_hvruntime(containerParams->opts, uvm_image, kernel, initrd, boot);
    }
    
    // Optional WCOW parent layers (flattened list).
    if (params->guest_windows.wcow_parent_layers_count != 0 && params->guest_windows.wcow_parent_layers != NULL) {
        wcow_parent_layers = environment_unflatten((const char*)params->guest_windows.wcow_parent_layers);
        if (wcow_parent_layers == NULL) {
            VLOG_ERROR("cvd", "cvd_create: failed to parse wcow_parent_layers\n");
            return CHEF_STATUS_INTERNAL_ERROR;
        }

        for (int i = 0; wcow_parent_layers[i] != NULL; ++i) {
            wcow_parent_layer_count++;
        }

        containerv_options_set_windows_wcow_parent_layers(
            containerParams->opts,
            (const char* const*)wcow_parent_layers,
            wcow_parent_layer_count);
    }
    
    // Compose layers into final rootfs
    status = containerv_layers_compose_with_options(
        containerParams->layers,
        containerParams->layers_count,
        containerParams->id,
        containerParams->opts,
        &containerParams->layer_context
    );

    if (status != 0) {
        VLOG_ERROR("cvd", "cvd_create: failed to compose layers\n");
        environment_destroy(wcow_parent_layers);
        return CHEF_STATUS_FAILED_ROOTFS_SETUP;
    }

    // Parent layers are only needed during compose.
    if (wcow_parent_layers != NULL) {
        environment_destroy(wcow_parent_layers);
        containerv_options_set_windows_wcow_parent_layers(containerParams->opts, NULL, 0);
    }

    containerv_options_set_layers(containerParams->opts, containerParams->layer_context);

    // setup policy
    policy = __policy_from_spec(&params->policy);
    if (policy != NULL) {
        VLOG_DEBUG("cvd", "cvd_create: applying security policy profiles\n");
        containerv_options_set_policy(containerParams->opts, policy);
    }

    // Optional network configuration
    if (__is_nonempty(params->network.container_ip) && __is_nonempty(params->network.container_netmask)) {
        containerv_options_set_network_ex(
            containerParams->opts,
            params->network.container_ip,
            params->network.container_netmask,
            __is_nonempty(params->network.host_ip) ? params->network.host_ip : NULL,
            __is_nonempty(params->network.gateway_ip) ? params->network.gateway_ip : NULL,
            __is_nonempty(params->network.dns) ? params->network.dns : NULL
        );
    }

    // setup other config
    enum containerv_capabilities caps =
        CV_CAP_FILESYSTEM |
        CV_CAP_PROCESS_CONTROL |
        CV_CAP_IPC;

    // Enable network capability if requested by policy profile or network configuration.
    if (__spec_contains_plugin(&params->policy, "network") ||
        (__is_nonempty(params->network.container_ip) 
            && __is_nonempty(params->network.container_netmask))) {
        caps |= CV_CAP_NETWORK;
    }

    containerv_options_set_caps(containerParams->opts, caps);

    // create the container
    status = containerv_create(
        containerParams->id,
        containerParams->opts,
        &containerParams->container
    );
    if (status) {
        VLOG_ERROR("cvd", "failed to start the container\n");
        return __chef_status_from_errno();
    }
    return CHEF_STATUS_SUCCESS;
}
#endif

enum chef_status cvd_create(const struct chef_create_parameters* params, const char** id)
{
    struct __create_container_params containerParams = { 0 };
    struct __container*              _container;
    char                             cvdIDBuffer[17];
    enum chef_status                 status;
    VLOG_DEBUG("cvd", "cvd_create()\n");

    if (params->layers_count == 0) {
        VLOG_ERROR("cvd", "cvd_create: no layers specified\n");
        return CHEF_STATUS_INVALID_MOUNTS;
    }

    if (params->id == NULL || strlen(params->id) == 0) {
        platform_secure_random_string(&cvdIDBuffer[0], sizeof(cvdIDBuffer) - 1);
        cvdIDBuffer[sizeof(cvdIDBuffer) - 1] = '\0';
        containerParams.id = &cvdIDBuffer[0];
    } else {
        containerParams.id = params->id;
    }
    VLOG_TRACE("cvd", "cvd_create: container ID %s\n", containerParams.id);

    containerParams.opts = containerv_options_new();
    if (containerParams.opts == NULL) {
        VLOG_ERROR("cvd", "failed to allocate memory for container options\n");
        return __chef_status_from_errno();
    }

    VLOG_DEBUG("cvd", "cvd_create: using layer-based approach with %d layers\n", params->layers_count);
    containerParams.layers = __to_cv_layers(params->layers, params->layers_count);
    if (containerParams.layers == NULL) {
        VLOG_ERROR("cvd", "cvd_create: failed to convert layers\n");
        containerv_options_delete(containerParams.opts);
        return CHEF_STATUS_INTERNAL_ERROR;
    }

    containerParams.layers_count = (int)params->layers_count;

#ifdef CHEF_ON_LINUX
    status = __create_linux_container(params, &containerParams);
#elif CHEF_ON_WINDOWS
    status = __create_hyperv_container(params, &containerParams);
#endif
    
    // this will free all non-essential members of containerParams
    __create_container_params_cleanup(&containerParams);
    if (status != CHEF_STATUS_SUCCESS) {
        VLOG_ERROR("cvd", "cvd_create: failed to setup & create container\n");
        if (containerParams.container != NULL) {
            containerv_destroy(containerParams.container);
        }
        containerv_layers_destroy(containerParams.layer_context);
        return status;
    }

    _container = __container_new(containerParams.container, containerParams.layer_context);
    if (_container == NULL) {
        VLOG_ERROR("cvd", "failed to allocate memory for the container structure\n");
        if (containerParams.container != NULL) {
            containerv_destroy(containerParams.container);
        }
        containerv_layers_destroy(containerParams.layer_context);
        return __chef_status_from_errno();
    }

    // Store the layer context for cleanup later
    _container->layer_context = containerParams.layer_context;
    
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
    int                             status;
    unsigned int                    public_id;
    process_handle_t                handle = 0;
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
    struct containerv_spawn_options opts = {
        .arguments = arguments,
        .environment = (const char* const*)environment,
        .flags = __convert_to_spawn_flags(params->options)
    };
    status = containerv_spawn(
        container->handle,
        command,
        &opts,
        &handle
    );
    if (status) {
        VLOG_ERROR("cvd", "cvd_spawn: failed to execute %s\n", command);
        ret = __chef_status_from_errno();
        goto cleanup;
    }

    public_id = __container_register_process(container, handle);
    if (public_id == 0) {
        VLOG_ERROR("cvd", "cvd_spawn: failed to register process handle\n");
        containerv_kill(container->handle, handle);
        ret = CHEF_STATUS_INTERNAL_ERROR;
        goto cleanup;
    }

    if (pIDOut != NULL) {
        *pIDOut = public_id;
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
    struct __container_process* proc;
    int                 status;
    VLOG_DEBUG("cvd", "cvd_kill(id=%s, pid=%u)\n", containerID, pID);

    // find container
    container = __find_container(containerID);
    if (container == NULL) {
        VLOG_ERROR("cvd", "cvd_kill: failed to find container %s", containerID);
        return CHEF_STATUS_INVALID_CONTAINER_ID;
    }

    proc = __container_find_process(container, pID);
    if (proc == NULL) {
        VLOG_ERROR("cvd", "cvd_kill: unknown process id %u\n", pID);
        return CHEF_STATUS_INTERNAL_ERROR;
    }

    status = containerv_kill(container->handle, proc->handle);
    if (status) {
        VLOG_ERROR("cvd", "cvd_kill: failed to kill process %u\n", pID);
        return __chef_status_from_errno();
    }

    list_remove(&container->processes, &proc->item_header);
    __container_process_delete(proc);
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
