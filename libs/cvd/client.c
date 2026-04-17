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

#include <chef/environment.h>
#include <chef/cvd.h>
#include <chef/containerv/disk/ubuntu.h>
#include <chef/bits/package.h>
#include <chef/dirs.h>
#include <chef/ingredient.h>
#include <chef/platform.h>
#include <chef/runtime.h>
#include <chef/store.h>
#include <chef/config.h>
#include <gracht/link/socket.h>
#include <gracht/client.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <vlog.h>

#include "chef_cvd_service_client.h"

#if defined(__linux__)
#include <arpa/inet.h>
#include <sys/un.h>

static int __abstract_socket_size(const char* address) {
    return offsetof(struct sockaddr_un, sun_path) + strlen(address);
}

static int __local_size(const char* address) {
    // If the address starts with '@', it is an abstract socket path.
    // Abstract socket paths are not null-terminated, so we need to account for that.
    if (address[0] == '@') {
        return __abstract_socket_size(address);
    }
    return sizeof(struct sockaddr_un);
}

static int __configure_local(struct sockaddr_storage* storage, const char* address)
{
    struct sockaddr_un* local = (struct sockaddr_un*)storage;

    local->sun_family = AF_LOCAL;

    // sanitize the address length
    if (strlen(address) >= sizeof(local->sun_path)) {
        fprintf(stderr, "__configure_local: address too long for local socket: %s\n", address);
        return -1;
    }

    // handle abstract socket paths
    if (address[0] == '@') {
        local->sun_path[0] = '\0';
        strncpy(local->sun_path + 1, address + 1, sizeof(local->sun_path) - 1);
    } else {
        strncpy(local->sun_path, address, sizeof(local->sun_path));
    }
    return 0;
}

static int __configure_local_bind(struct gracht_link_socket* link)
{
    struct sockaddr_storage storage = { 0 };
    struct sockaddr_un*     address = (struct sockaddr_un*)&storage;

    address->sun_family = AF_LOCAL;
    snprintf(&address->sun_path[1],
        sizeof(address->sun_path) - 2,
        "/chef/cvd/clients/%u",
        getpid()
    );

    gracht_link_socket_set_bind_address(link, &storage, __abstract_socket_size(&address->sun_path[1]));
    return 0;
}
#elif defined(_WIN32)
#include <windows.h>
#include <process.h>
#include <ws2ipdef.h>

// Windows 10 Insider build 17063 ++ 
#include <afunix.h>

static int __abstract_socket_size(const char* address) {
    return (int)(offsetof(struct sockaddr_un, sun_path) + strlen(address));
}

static int __local_size(const char* address) {
    return sizeof(struct sockaddr_un);
}

static int __configure_local(struct sockaddr_storage* storage, const char* address)
{
    struct sockaddr_un* local = (struct sockaddr_un*)storage;

    local->sun_family = AF_UNIX;
    strncpy(local->sun_path, address, sizeof(local->sun_path));
    return 0;
}

static int __configure_local_bind(struct gracht_link_socket* link)
{
    struct sockaddr_storage storage = { 0 };
    struct sockaddr_un*     address = (struct sockaddr_un*)&storage;

    address->sun_family = AF_UNIX;
    snprintf(&address->sun_path[1],
        sizeof(address->sun_path) - 2,
        "/chef/cvd/clients/%u",
        _getpid()
    );

    gracht_link_socket_set_bind_address(link, &storage, __abstract_socket_size(&address->sun_path[1]));
    return 0;
}
#endif

static void __configure_inet4(struct sockaddr_storage* storage, struct chef_config_address* config)
{
    struct sockaddr_in* inet4 = (struct sockaddr_in*)storage;

    inet4->sin_family = AF_INET;
    inet4->sin_addr.s_addr = inet_addr(config->address);
    inet4->sin_port = htons(config->port);
}

static int init_link_config(struct gracht_link_socket* link, enum gracht_link_type type, struct chef_config_address* config)
{
    struct sockaddr_storage addr_storage = { 0 };
    socklen_t               size;
    int                     domain = 0;
    int                     status;
    VLOG_DEBUG("bake", "init_link_config(link=%i, type=%s)\n", type, config->type);

    if (!strcmp(config->type, "local")) {
        status = __configure_local_bind(link);
        if (status) {
            VLOG_ERROR("bake", "__init_link_config failed to configure local bind address\n");
            return status;
        }

        status = __configure_local(&addr_storage, config->address);
        if (status) {
            VLOG_ERROR("bake", "init_link_config failed to configure local link\n");
            return status;
        }
        domain = addr_storage.ss_family;
        size = __local_size(config->address);

        VLOG_DEBUG("bake", "connecting to %s\n", config->address);
    } else if (!strcmp(config->type, "inet4")) {
        __configure_inet4(&addr_storage, config);
        domain = AF_INET;
        size = sizeof(struct sockaddr_in);
        
        VLOG_DEBUG("bake", "connecting to %s:%u\n", config->address, config->port);
    } else if (!strcmp(config->type, "inet6")) {
        // TODO
        domain = AF_INET6;
        size = sizeof(struct sockaddr_in6);
    } else {
        VLOG_ERROR("bake", "init_link_config invalid link type %s\n", config->type);
        return -1;
    }

    gracht_link_socket_set_type(link, type);
    gracht_link_socket_set_connect_address(link, (const struct sockaddr_storage*)&addr_storage, size);
    gracht_link_socket_set_domain(link, domain);
    return 0;
}

int bake_client_initialize(struct __bake_build_context* bctx)
{
    struct gracht_link_socket*         link;
    struct gracht_client_configuration clientConfiguration;
    int                                code;
    VLOG_DEBUG("bake", "kitchen_client_initialize()\n");

    code = gracht_link_socket_create(&link);
    if (code) {
        VLOG_ERROR("bake", "kitchen_client_initialize: failed to initialize socket\n");
        return code;
    }

    init_link_config(link, gracht_link_packet_based, &bctx->cvd_address);

    gracht_client_configuration_init(&clientConfiguration);
    gracht_client_configuration_set_link(&clientConfiguration, (struct gracht_link*)link);

    code = gracht_client_create(&clientConfiguration, &bctx->cvd_client);
    if (code) {
        VLOG_ERROR("bake", "kitchen_client_initialize: error initializing client library %i, %i\n", errno, code);
        return code;
    }

    code = gracht_client_connect(bctx->cvd_client);
    if (code) {
        VLOG_ERROR("bake", "kitchen_client_initialize: failed to connect client %i, %i\n", errno, code);
        gracht_client_shutdown(bctx->cvd_client);
        bctx->cvd_client = NULL;
        return code;
    }

    return code;
}


static enum chef_status __chef_status_from_errno(void) {
    switch (errno) {

        default:
            return CHEF_STATUS_INTERNAL_ERROR;
    }
}

static int __is_windows_guest_target(const char* target_platform, const char* base)
{
    if (target_platform != NULL && strncmp(target_platform, "windows", 7) == 0) {
        return 1;
    }

    if (base != NULL && strncmp(base, "windows", 7) == 0) {
        return 1;
    }
    return 0;
}

static int __path_is_directory(const char* path)
{
    struct platform_stat st;

    if (path == NULL || path[0] == '\0') {
        return 0;
    }

    return (platform_stat(path, &st) == 0 && st.type == PLATFORM_FILETYPE_DIRECTORY) ? 1 : 0;
}

static int __path_exists(const char* path)
{
    struct platform_stat st;

    if (path == NULL || path[0] == '\0') {
        return 0;
    }

    return platform_stat(path, &st) == 0 ? 1 : 0;
}

static char* __path_join_if_exists(const char* base, const char* name)
{
    char* candidate;

    if (base == NULL || name == NULL) {
        return NULL;
    }

    candidate = strpathcombine(base, name);
    if (candidate == NULL) {
        return NULL;
    }

    if (!__path_exists(candidate)) {
        free(candidate);
        return NULL;
    }
    return candidate;
}

static char* __lookup_registered_base_path(const char* base)
{
    struct chef_config* config;
    void*               section;
    const char*         value;

    if (base == NULL || base[0] == '\0') {
        return NULL;
    }

    config = chef_config_load(chef_dirs_config());
    if (config == NULL) {
        return NULL;
    }

    section = chef_config_section(config, "base-images");
    value = chef_config_get_string(config, section, base);
    if (value == NULL || value[0] == '\0') {
        return NULL;
    }
    return platform_strdup(value);
}

static char* __resolve_store_base_pack(const char* base, const char* target_platform, const char* target_architecture)
{
    static const char* identities[] = {
        "vali",
        NULL
    };

    for (int i = 0; identities[i] != NULL; ++i) {
        char*       store_id;
        int         revision;
        const char* pack_path = NULL;

        store_id = chef_runtime_base_to_store_id(identities[i], base);
        if (store_id == NULL) {
            continue;
        }

        revision = store_ensure_package(&(struct store_package) {
                .name = store_id,
                .platform = target_platform,
                .arch = target_architecture,
                .channel = "stable"
            }, NULL);
        if (revision > 0 && store_package_path(&(struct store_package) {
                .name = store_id,
                .platform = target_platform,
                .arch = target_architecture,
                .revision = revision
            }, &pack_path) == 0 && pack_path != NULL) {
            char* resolved = platform_strdup(pack_path);
            free(store_id);
            return resolved;
        }
        free(store_id);
    }
    return NULL;
}

static int __materialize_pack_to_directory(const char* pack_path, const char* output_dir)
{
    struct ingredient* ingredient;
    int                status;

    status = ingredient_open(pack_path, &ingredient);
    if (status != 0) {
        return -1;
    }

    status = ingredient_unpack(ingredient, output_dir, NULL, NULL);
    ingredient_close(ingredient);
    return status;
}

static int __resolve_windows_rootfs_layout(const char* source_path, char** rootfs_out, char** utilityvm_out)
{
    char* windowsfilter;

    if (rootfs_out == NULL || utilityvm_out == NULL) {
        return -1;
    }

    *rootfs_out = NULL;
    *utilityvm_out = NULL;

    if (source_path == NULL || source_path[0] == '\0') {
        return -1;
    }

    windowsfilter = __path_join_if_exists(source_path, "windowsfilter");
    if (windowsfilter != NULL && __path_is_directory(windowsfilter)) {
        *rootfs_out = windowsfilter;
        *utilityvm_out = __path_join_if_exists(source_path, "UtilityVM");
        return 0;
    }

    if (__path_is_directory(source_path)) {
        char* layerchain = __path_join_if_exists(source_path, "layerchain.json");
        if (layerchain != NULL) {
            free(layerchain);
            *rootfs_out = platform_strdup(source_path);
            return *rootfs_out == NULL ? -1 : 0;
        }
    }
    return -1;
}

static void __initialize_layers(
    struct chef_create_parameters* params,
    const char*                    rootfs,
    struct __bake_build_context*   bctx,
    enum chef_guest_type           guest_type)
{
    struct chef_layer_descriptor* layer;
    const char*                   project_target;
    const char*                   store_target;
    const uint32_t                layer_count = guest_type == CHEF_GUEST_TYPE_WINDOWS ? 3U : 4U;
    VLOG_DEBUG("cvd", "__initialize_layers(rootfs=%s, guest=%s)\n", rootfs, guest_type == CHEF_GUEST_TYPE_WINDOWS ? "windows" : "linux");

    chef_create_parameters_layers_add(params, layer_count);

    project_target = guest_type == CHEF_GUEST_TYPE_WINDOWS ? "C:\\chef\\project" : "/chef/project";
    store_target = guest_type == CHEF_GUEST_TYPE_WINDOWS ? "C:\\chef\\store" : "/chef/store";

    // setup the base rootfs
    layer = chef_create_parameters_layers_get(params, 0);
    layer->type = CHEF_LAYER_TYPE_BASE_ROOTFS;
    layer->source = platform_strdup(rootfs);
    layer->target = platform_strdup("/");
    layer->options = 0;

    // setup the project mount
    layer = chef_create_parameters_layers_get(params, 1);
    layer->type = CHEF_LAYER_TYPE_HOST_DIRECTORY;
    layer->source = platform_strdup(bctx->host_cwd);
    layer->target = platform_strdup(project_target);
    layer->options = CHEF_MOUNT_OPTIONS_READONLY;

    // setup the store mount
    layer = chef_create_parameters_layers_get(params, 2);
    layer->type = CHEF_LAYER_TYPE_HOST_DIRECTORY;
    layer->source = platform_strdup(chef_dirs_store());
    layer->target = platform_strdup(store_target);
    layer->options = CHEF_MOUNT_OPTIONS_READONLY;

    if (guest_type == CHEF_GUEST_TYPE_LINUX) {
        // initialize the overlay layer, this is an writable layer
        // to capture all the changes
        layer = chef_create_parameters_layers_get(params, 3);
        layer->type = CHEF_LAYER_TYPE_OVERLAY;
    }
}

// Initialize the base rootfs for the build container if, and only if, it's not already
// initialized. We use the build cache, and check key "rootfs-initialized" to see if we've
// already done this.
static char* __initialize_maybe_rootfs(
    struct recipe*       recipe,
    const char*          target_platform,
    const char*          target_architecture,
    enum chef_guest_type guest_type,
    struct build_cache*  cache,
    char**               utilityvm_out)
{
    const char* base;
    const char* configured_rootfs;
    char*       registered_rootfs = NULL;
    char*       pack_path = NULL;
    char*       rootfs;
    char*       unpacked_rootfs = NULL;
    char*       utilityvm_path = NULL;
    int         status;
    VLOG_DEBUG("bake", "__initialize_maybe_rootfs(uuid=%s)\n", build_cache_uuid(cache));

    if (utilityvm_out != NULL) {
        *utilityvm_out = NULL;
    }

    base = recipe_platform_base(recipe, target_platform);
    if (base == NULL) {
        VLOG_ERROR("cvd", "failed to determine base rootfs for platform %s\n", target_platform);
        return NULL;
    }

    if (guest_type == CHEF_GUEST_TYPE_WINDOWS) {
        configured_rootfs = build_cache_key_string(cache, "rootfs-path");
        if (__resolve_windows_rootfs_layout(configured_rootfs, &unpacked_rootfs, &utilityvm_path) == 0) {
            if (utilityvm_out != NULL) {
                *utilityvm_out = utilityvm_path;
            } else {
                free(utilityvm_path);
            }
            return unpacked_rootfs;
        }

        registered_rootfs = __lookup_registered_base_path(base);
        if (__resolve_windows_rootfs_layout(registered_rootfs, &unpacked_rootfs, &utilityvm_path) == 0) {
            free(registered_rootfs);
            if (utilityvm_out != NULL) {
                *utilityvm_out = utilityvm_path;
            } else {
                free(utilityvm_path);
            }
            return unpacked_rootfs;
        }
        free(registered_rootfs);

        if (__resolve_windows_rootfs_layout(base, &unpacked_rootfs, &utilityvm_path) == 0) {
            if (utilityvm_out != NULL) {
                *utilityvm_out = utilityvm_path;
            } else {
                free(utilityvm_path);
            }
            return unpacked_rootfs;
        }

        pack_path = __resolve_store_base_pack(base, target_platform, target_architecture);
        if (pack_path != NULL) {
            rootfs = chef_dirs_rootfs_new(build_cache_uuid(cache));
            if (rootfs == NULL) {
                free(pack_path);
                return NULL;
            }

            if (!build_cache_key_bool(cache, "rootfs-initialized")) {
                status = __materialize_pack_to_directory(pack_path, rootfs);
                if (status != 0) {
                    free(pack_path);
                    free(rootfs);
                    return NULL;
                }

                build_cache_transaction_begin(cache);
                build_cache_key_set_bool(cache, "rootfs-initialized", 1);
                build_cache_key_set_string(cache, "rootfs-path", rootfs);
                build_cache_transaction_commit(cache);
            }
            free(pack_path);

            if (__resolve_windows_rootfs_layout(rootfs, &unpacked_rootfs, &utilityvm_path) == 0) {
                free(rootfs);
                if (utilityvm_out != NULL) {
                    *utilityvm_out = utilityvm_path;
                } else {
                    free(utilityvm_path);
                }
                return unpacked_rootfs;
            }
            free(rootfs);
        }

        VLOG_ERROR(
            "bake",
            "__initialize_maybe_rootfs: windows target %s requires a registered base image path, a pre-prepared windows rootfs directory, or an installed OS base pack for %s\n",
            target_platform,
            base);
        return NULL;
    }

    rootfs = chef_dirs_rootfs_new(build_cache_uuid(cache));
    if (rootfs == NULL) {
        VLOG_ERROR("bake", "__initialize_maybe_rootfs: failed to allocate memory for rootfs\n");
        return NULL;
    }

    if (build_cache_key_bool(cache, "rootfs-initialized")) {
        VLOG_DEBUG("bake", "__initialize_maybe_rootfs: rootfs already initialized, skipping\n");
        return rootfs;
    }

    status = containerv_disk_setup_ubuntu_rootfs(rootfs, base);
    if (status) {
        VLOG_ERROR("cvd", "failed to resolve the rootfs image\n");
        free(rootfs);
        return NULL;
    }

    build_cache_transaction_begin(cache);
    build_cache_key_set_bool(cache, "rootfs-initialized", 1);
    build_cache_transaction_commit(cache);
    return rootfs;
}

#ifdef CHEF_ON_WINDOWS
static int __initialize_maybe_lcow_uvm(struct chef_create_parameters* params)
{
    (void)params;
    return 0;
}
#endif

enum chef_status bake_client_create_container(struct __bake_build_context* bctx)
{
    struct gracht_message_context context;
    struct chef_create_parameters params;
    const char*                   base;
    int                           status;
    enum chef_status              chstatus;
    char*                         rootfs = NULL;
    char*                         utilityvm_path = NULL;
    char                          cvdid[64];
    VLOG_DEBUG("bake", "bake_client_create_container()\n");
    
    chef_create_parameters_init(&params);

    base = recipe_platform_base(bctx->recipe, bctx->target_platform);
    
    params.id = platform_strdup(build_cache_uuid(bctx->build_cache));
    params.gtype = __is_windows_guest_target(bctx->target_platform, base) ? CHEF_GUEST_TYPE_WINDOWS : CHEF_GUEST_TYPE_LINUX;
    VLOG_DEBUG("bake", "bake_client_create_container: target_platform=%s base=%s guest=%s\n",
        bctx->target_platform,
        base != NULL ? base : "(null)",
        params.gtype == CHEF_GUEST_TYPE_WINDOWS ? "windows" : "linux");

    chef_policy_spec_init(&params.policy);
    chef_policy_spec_plugins_add(&params.policy, 2);

    // Setup build container policy plugins
    chef_policy_spec_plugins_get(&params.policy, 0)->name = platform_strdup("file-control");
    chef_policy_spec_plugins_get(&params.policy, 1)->name = platform_strdup("package-management");
    
    // On windows, linux containers require special UVM setup in addition
    // to the rootfs overlay. The UVM setup is done here.
#ifdef CHEF_ON_WINDOWS
    if (params.gtype == CHEF_GUEST_TYPE_LINUX) {
        if (__initialize_maybe_lcow_uvm(&params) != 0) {
            chef_create_parameters_destroy(&params);
            return CHEF_STATUS_FAILED_ROOTFS_SETUP;
        }
    }
#endif
    
    rootfs = __initialize_maybe_rootfs(
        bctx->recipe,
        bctx->target_platform,
        bctx->target_architecture,
        params.gtype,
        bctx->build_cache,
        &utilityvm_path);
    if (rootfs == NULL) {
        chef_create_parameters_destroy(&params);
        VLOG_ERROR("bake", "bake_client_create_container: failed to resolve build rootfs\n");
        return CHEF_STATUS_FAILED_ROOTFS_SETUP;
    }

    if (params.gtype == CHEF_GUEST_TYPE_WINDOWS && utilityvm_path != NULL) {
        params.guest_windows.wcow_utilityvm_path = platform_strdup(utilityvm_path);
    }

    __initialize_layers(&params, rootfs, bctx, params.gtype);
    
    status = chef_cvd_create(bctx->cvd_client, &context, &params);
    
    chef_create_parameters_destroy(&params);
    free(rootfs);
    free(utilityvm_path);
    if (status) {
        VLOG_ERROR("bake", "bake_client_create_container failed to create client\n");
        return status;
    }

    gracht_client_wait_message(bctx->cvd_client, &context, GRACHT_MESSAGE_BLOCK);
    chef_cvd_create_result(bctx->cvd_client, &context, &cvdid[0], sizeof(cvdid) - 1, &chstatus);
    if (chstatus == CHEF_STATUS_SUCCESS) {
        bctx->cvd_id = platform_strdup(&cvdid[0]);
        if (bctx->cvd_id == NULL) {
            VLOG_FATAL("bake", "failed to allocate memory for CVD id\n");
        }
    }
    return chstatus;
}

enum chef_status bake_client_spawn(
    struct __bake_build_context* bctx,
    const char*                  command,
    enum chef_spawn_options      options,
    unsigned int*                pidOut)
{
    struct gracht_message_context context;
    int                           status;
    enum chef_status              chstatus;
    uint8_t*                      flatenv = NULL;
    size_t                        flatenvLength = 0;
    VLOG_DEBUG("bake", "bake_client_spawn(cmd=%s)\n", command);

    if (bctx->base_environment != NULL) {
        flatenv = environment_flatten(bctx->base_environment, &flatenvLength);
        if (flatenv == NULL) {
            return CHEF_STATUS_INTERNAL_ERROR;
        }
    }

    status = chef_cvd_spawn(
        bctx->cvd_client,
        &context,
        &(struct chef_spawn_parameters) {
            .container_id = bctx->cvd_id,
            .command = (char*)command,
            .options = options,
            .environment = flatenv,
            .environment_count = (uint32_t)flatenvLength
            /* .user = */
        }
    );
    if (status != 0) {
        VLOG_ERROR("bake", "bake_client_spawn: failed to execute %s\n", command);
        return status;
    }
    gracht_client_wait_message(bctx->cvd_client, &context, GRACHT_MESSAGE_BLOCK);
    chef_cvd_spawn_result(bctx->cvd_client, &context, pidOut, &chstatus);
    return chstatus;
}

enum chef_status bake_client_upload(struct __bake_build_context* bctx, const char* hostPath, const char* containerPath)
{
    struct gracht_message_context context;
    int                           status;
    enum chef_status              chstatus;
    VLOG_DEBUG("bake", "bake_client_upload(host=%s, child=%s)\n", hostPath, containerPath);

    status = chef_cvd_upload(
        bctx->cvd_client,
        &context,
        &(struct chef_file_parameters) {
            .container_id = bctx->cvd_id,
            .source_path = (char*)hostPath,
            .destination_path = (char*)containerPath,
            .user.username = ""
        }
    );
    if (status != 0) {
        VLOG_ERROR("bake", "bake_client_upload: failed to upload %s\n", hostPath);
        return status;
    }
    gracht_client_wait_message(bctx->cvd_client, &context, GRACHT_MESSAGE_BLOCK);
    chef_cvd_upload_result(bctx->cvd_client, &context, &chstatus);
    return chstatus;
}

enum chef_status bake_client_destroy_container(struct __bake_build_context* bctx)
{
    struct gracht_message_context context;
    int                           status;
    enum chef_status              chstatus;
    VLOG_DEBUG("bake", "bake_client_destroy_container()\n");

    if (bctx->cvd_id == NULL) {
        VLOG_DEBUG("bake", "bake_client_destroy_container: no container to destroy\n");
        return CHEF_STATUS_SUCCESS;
    }

    status = chef_cvd_destroy(bctx->cvd_client, &context, bctx->cvd_id);
    if (status != 0) {
        VLOG_ERROR("bake", "bake_client_destroy_container: failed to invoke destroy\n");
        return status;
    }
    gracht_client_wait_message(bctx->cvd_client, &context, GRACHT_MESSAGE_BLOCK);
    chef_cvd_destroy_result(bctx->cvd_client, &context, &chstatus);

    // make sure we do not retry destruction
    if (chstatus == CHEF_STATUS_SUCCESS) {
        free(bctx->cvd_id);
        bctx->cvd_id = NULL;
    }
    return chstatus;
}
