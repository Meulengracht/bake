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
#include <chef/dirs.h>
#include <chef/platform.h>
#include <gracht/link/socket.h>
#include <gracht/client.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <vlog.h>

#include "chef_cvd_service_client.h"
#include "ubuntu.h"

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

// Windows 10 Insider build 17063 ++ 
#include <afunix.h>

static int __local_size(const char* address) {
    return sizeof(struct sockaddr_un);
}

static int __configure_local(struct sockaddr_storage* storage, const char* address)
{
    struct sockaddr_un* local = (struct sockaddr_un*)storage;

    local->sun_family = AF_LOCAL;
    strncpy(local->sun_path, address, sizeof(local->sun_path));
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
        domain = AF_LOCAL;
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

static int __file_exists(const char* path)
{
    struct platform_stat stats;
    return platform_stat(path, &stats) == 0 ? 1 : 0;
}

#ifdef CHEF_ON_LINUX
static int __fixup_dns(const char* rootfs)
{
    int   status;
    char  tmp[PATH_MAX];
    FILE* stream;

    snprintf(
        &tmp[0],
        sizeof(tmp),
        "%s/etc/resolv.conf", 
        rootfs
    );
    VLOG_DEBUG("cvd", "__fixup_dns(dns=%s)\n", &tmp[0]);

    // the rootfs may come with dns that relies on the host
    // but for these types of rootfs static is fine
    status = unlink(&tmp[0]);
    if (status) {
        VLOG_WARNING("cvd", "__fixup_dns: failed to unlink %s, trying anyway\n", &tmp[0]);
    }

    stream = fopen(&tmp[0], "w+");
    if (stream == NULL) {
        VLOG_ERROR("cvd", "__fixup_dns: failed to open %s\n", &tmp[0]);
        return -1;
    }

    fprintf(stream, "# generated by cvd\n");
    fprintf(stream, "nameserver 8.8.4.4\n");
    return fclose(stream);
}

static int __download_base(const char* base, const char* dir)
{
    char  tmp[PATH_MAX];
    int   status;
    char* url = __ubuntu_get_base_image_url(base);
    if (url == NULL) {
        VLOG_ERROR("cvd", "failed to allocate memory for base image url\n");
        return -1;
    }

    snprintf(&tmp[0], sizeof(tmp), "-P %s %s", dir, url);

    VLOG_TRACE("cvd", "downloading %s\n", url);
    status = platform_spawn(
        "wget", &tmp[0], NULL, &(struct platform_spawn_options) { }
    );
    if (status) {
        VLOG_ERROR("cvd", "failed to download ubuntu rootfs\n");
    }
    free(url);
    return status;
}

static int __ensure_base_rootfs(const char* rootfs, const char* base)
{
    char* imageCache = NULL;
    char* imageName = NULL;
    char  tmp[PATH_MAX];
    int   status;
    VLOG_DEBUG("cvd", "__ensure_base_rootfs()\n");
    
    imageCache = strpathcombine(chef_dirs_cache(), "rootfs");
    if (imageCache == NULL) {
        VLOG_ERROR("cvd", "failed to allocate memory for rootfs path\n");
        return -1;
    }

    status = platform_mkdir(imageCache);
    if (status) {
        VLOG_ERROR("cvd", "failed to create directory %s\n", imageCache);
        goto exit;
    }

    imageName = __ubuntu_get_base_image_name(base);
    if (imageName == NULL) {
        VLOG_ERROR("cvd", "failed to allocate memory for base image name\n");
        status = -1;
        goto exit;
    }

    snprintf(&tmp[0], sizeof(tmp), "%s/%s", imageCache, imageName);

    if (!__file_exists(&tmp[0])) {
        status = __download_base(base, imageCache);
        if (status) {
            VLOG_ERROR("cvd", "failed to download ubuntu rootfs\n");
            goto exit;
        }
    }

    snprintf(
        &tmp[0],
        sizeof(tmp),
        "-x --xattrs-include=* -f %s/%s -C %s",
        imageCache, imageName, rootfs
    );

    VLOG_TRACE("cvd", "unpacking %s/%s\n", imageCache, imageName);
    status = platform_spawn(
        "tar", &tmp[0], NULL, &(struct platform_spawn_options) {
        }
    );
    if (status) {
        VLOG_ERROR("cvd", "failed to download ubuntu rootfs\n");
        goto exit;
    }

    status = __fixup_dns(rootfs);
    if (status) {
        VLOG_ERROR("cvd", "failed to fix dns settings\n");
        goto exit;
    }

exit:
    free(imageCache);
    free(imageName);
    return status;
}

#elif CHEF_ON_WINDOWS
static int __ensure_base_rootfs(const char* rootfs, const char* base) {
    VLOG_DEBUG("cvd", "__ensure_base_rootfs(rootfs=%s) - Windows implementation\n", rootfs);
    
    // On Windows, we would typically:
    // 1. Download or use a Windows container base image
    // 2. Or use WSL2 for Linux containers
    // For now, just ensure the directory exists
    
    int status = platform_mkdir(rootfs);
    if (status != 0) {
        // Directory creation failed - check if it already exists
        if (errno != EEXIST) {
            VLOG_ERROR("cvd", "failed to create rootfs directory %s\n", rootfs);
            return -1;
        }
        // Directory already exists, that's okay
    }
    
    // TODO: Implement Windows container base image setup
    // This could involve:
    // - Pulling a Windows Server Core or Nano Server base image
    // - Setting up a WSL2 distribution for Linux containers
    // - Configuring the HyperV VM with the base image
    
    VLOG_WARNING("cvd", "Windows base rootfs setup not fully implemented yet\n");
    return 0;
}
#endif

static void __initialize_overlays(struct chef_create_parameters* params, const char* rootfs, struct __bake_build_context* bctx)
{
    struct chef_layer_descriptor* layer;
    VLOG_DEBUG("cvd", "__initialize_overlays(rootfs=%s)\n", rootfs);

    chef_create_parameters_layers_add(params, 4);

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
    layer->target = platform_strdup("/chef/project");
    layer->options = CHEF_MOUNT_OPTIONS_READONLY;

    // setup the store mount
    layer = chef_create_parameters_layers_get(params, 2);
    layer->type = CHEF_LAYER_TYPE_HOST_DIRECTORY;
    layer->source = platform_strdup(chef_dirs_store());
    layer->target = platform_strdup("/chef/store");
    layer->options = CHEF_MOUNT_OPTIONS_READONLY;

    // initialize the overlay layer, this is an writable layer
    // to capture all the changes
    layer = chef_create_parameters_layers_get(params, 3);
    layer->type = CHEF_LAYER_TYPE_OVERLAY;
}

// Initialize the base rootfs for the build container if, and only if, it's not already
// initialized. We use the build cache, and check key "rootfs-initialized" to see if we've
// already done this.
static char* __initialize_maybe_rootfs(struct recipe* recipe, struct build_cache* cache)
{
    char* rootfs;
    int   status;
    VLOG_DEBUG("bake", "__initialize_maybe_rootfs(uuid=%s)\n", build_cache_uuid(cache));

    rootfs = (char*)chef_dirs_rootfs(build_cache_uuid(cache));
    if (rootfs == NULL) {
        VLOG_ERROR("bake", "__initialize_maybe_rootfs: failed to allocate memory for rootfs\n");
        return NULL;
    }

    if (build_cache_key_bool(cache, "rootfs-initialized")) {
        VLOG_DEBUG("bake", "__initialize_maybe_rootfs: rootfs already initialized, skipping\n");
        return rootfs;
    }

    status = platform_mkdir(rootfs);
    if (status) {
        VLOG_ERROR("cvd", "failed to create directory %s\n", rootfs);
        return NULL;
    }

    status = __ensure_base_rootfs(rootfs, recipe_platform_base(recipe, CHEF_PLATFORM_STR));
    if (status) {
        VLOG_ERROR("cvd", "failed to resolve the rootfs image\n");
        return NULL;
    }

    build_cache_transaction_begin(cache);
    build_cache_key_set_bool(cache, "rootfs-initialized", 1);
    build_cache_transaction_commit(cache);
    return rootfs;
}

enum chef_status bake_client_create_container(struct __bake_build_context* bctx)
{
    struct gracht_message_context context;
    struct chef_create_parameters params;
    int                           status;
    enum chef_status              chstatus;
    char*                         rootfs;
    char                          cvdid[64];
    VLOG_DEBUG("bake", "bake_client_create_container()\n");
    
    rootfs = __initialize_maybe_rootfs(bctx->recipe, bctx->build_cache);
    if (rootfs == NULL) {
        VLOG_ERROR("bake", "bake_client_create_container: failed to allocate memory for rootfs\n");
        return CHEF_STATUS_FAILED_ROOTFS_SETUP;
    }

    chef_create_parameters_init(&params);
    __initialize_overlays(&params, rootfs, bctx);
    
    status = chef_cvd_create(bctx->cvd_client, &context, &params);
    
    chef_create_parameters_destroy(&params);
    free(rootfs);
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
            .environment_count = flatenvLength
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
