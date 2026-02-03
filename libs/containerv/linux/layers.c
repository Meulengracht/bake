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

#define FUSE_USE_VERSION 32

#include <chef/containerv/layers.h>
#include <chef/containerv.h>
#include <chef/platform.h>
#include <errno.h>
#include <fuse3/fuse.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <threads.h>
#include <unistd.h>
#include <vafs/vafs.h>
#include <vafs/file.h>
#include <vafs/directory.h>
#include <vafs/stat.h>
#include <vlog.h>

/**
 * @brief VaFS FUSE mount handle
 */
struct __vafs_mount {
    struct VaFs* vafs;
    struct fuse* fuse;
    char*        mount_point;
    thrd_t       worker;
};

/**
 * @brief Mounted layer information
 */
struct __mounted_layer {
    enum containerv_layer_type type;
    char*                      mount_point;   // Where layer is mounted
    char*                      source_path;   // Original source
    int                        readonly;      // For HOST_DIRECTORY layers
    void*                      handle;        // Mount handle (e.g., __vafs_mount*)
};

/**
 * @brief Layer context structure
 */
struct containerv_layer_context {
    struct __mounted_layer* layers;
    int                     layer_count;
    char*                   composed_rootfs;  // Final composed rootfs
    char*                   work_dir;         // OverlayFS work dir
    char*                   upper_dir;        // OverlayFS upper dir
    char*                   container_id;     // Container ID
    int                     overlay_mounted;  // Whether overlay was mounted
    int                     readonly;         // Read-only flag
};

// ============================================================================
// VaFS FUSE Implementation
// ============================================================================

static int __vafs_getattr(const char* path, struct stat* stbuf, struct fuse_file_info* fi)
{
    struct fuse_context* context = fuse_get_context();
    struct __vafs_mount* mount   = (struct __vafs_mount*)context->private_data;
    struct vafs_stat     vstat;
    int                  status;
    
    status = vafs_path_stat(mount->vafs, path, 1, &vstat);
    if (status) {
        return status;
    }
    
    memset(stbuf, 0, sizeof(struct stat));
    stbuf->st_mode = vstat.mode;
    stbuf->st_size = vstat.size;
    stbuf->st_uid = 0;
    stbuf->st_gid = 0;
    stbuf->st_nlink = 1;
    
    return 0;
}

static int __vafs_open(const char* path, struct fuse_file_info* fi)
{
    struct fuse_context*   context = fuse_get_context();
    struct __vafs_mount*   mount   = (struct __vafs_mount*)context->private_data;
    struct VaFsFileHandle* handle;
    int                    status;
    
    status = vafs_file_open(mount->vafs, path, &handle);
    if (status) {
        return status;
    }
    
    fi->fh = (uint64_t)handle;
    return 0;
}

static int __vafs_read(const char* path, char* buf, size_t size, off_t offset, struct fuse_file_info* fi)
{
    struct VaFsFileHandle* handle = (struct VaFsFileHandle*)fi->fh;
    size_t                 bytesRead;
    int                    status;
    
    status = vafs_file_seek(handle, SEEK_SET, offset);
    if (status) {
        return status;
    }
    
    bytesRead = vafs_file_read(handle, buf, size);
    return (int)bytesRead;
}

static int __vafs_release(const char* path, struct fuse_file_info* fi)
{
    struct VaFsFileHandle* handle = (struct VaFsFileHandle*)fi->fh;
    
    if (handle == NULL) {
        return -EINVAL;
    }
    
    vafs_file_close(handle);
    fi->fh = 0;
    return 0;
}

static int __vafs_opendir(const char* path, struct fuse_file_info* fi)
{
    struct fuse_context*        context = fuse_get_context();
    struct __vafs_mount*        mount   = (struct __vafs_mount*)context->private_data;
    struct VaFsDirectoryHandle* handle;
    int                         status;
    
    status = vafs_directory_open(mount->vafs, path, &handle);
    if (status) {
        return status;
    }
    
    fi->fh = (uint64_t)handle;
    return 0;
}

static int __vafs_readdir(const char* path, void* buf, fuse_fill_dir_t filler, 
                          off_t offset, struct fuse_file_info* fi, enum fuse_readdir_flags flags)
{
    struct VaFsDirectoryHandle* handle = (struct VaFsDirectoryHandle*)fi->fh;
    struct VaFsEntry            entry;
    
    filler(buf, ".", NULL, 0, 0);
    filler(buf, "..", NULL, 0, 0);
    
    while (vafs_directory_read(handle, &entry) == 0) {
        filler(buf, entry.Name, NULL, 0, 0);
    }
    
    return 0;
}

static int __vafs_releasedir(const char* path, struct fuse_file_info* fi)
{
    struct VaFsDirectoryHandle* handle = (struct VaFsDirectoryHandle*)fi->fh;
    
    if (handle == NULL) {
        return -EINVAL;
    }
    
    vafs_directory_close(handle);
    fi->fh = 0;
    return 0;
}

static const struct fuse_operations g_vafs_operations = {
    .getattr    = __vafs_getattr,
    .open       = __vafs_open,
    .read       = __vafs_read,
    .release    = __vafs_release,
    .opendir    = __vafs_opendir,
    .readdir    = __vafs_readdir,
    .releasedir = __vafs_releasedir,
};

static int __fuse_loop_wrapper(void* arg)
{
    struct fuse* fuse = (struct fuse*)arg;
    return fuse_loop(fuse);
}

static int __vafs_mount(const char* pack_path, const char* mount_point, struct __vafs_mount** mount_out)
{
    struct __vafs_mount* mount;
    struct fuse_args     args = FUSE_ARGS_INIT(0, NULL);
    int                  status;
    
    VLOG_DEBUG("containerv", "__vafs_mount: mounting %s at %s\n", pack_path, mount_point);
    
    mount = calloc(1, sizeof(struct __vafs_mount));
    if (mount == NULL) {
        return -1;
    }
    
    mount->mount_point = strdup(mount_point);
    if (mount->mount_point == NULL) {
        free(mount);
        return -1;
    }
    
    status = vafs_open_file(pack_path, &mount->vafs);
    if (status != 0) {
        VLOG_ERROR("containerv", "__vafs_mount: failed to open VaFS package\n");
        free(mount->mount_point);
        free(mount);
        return -1;
    }
    
    mount->fuse = fuse_new(&args, &g_vafs_operations, sizeof(g_vafs_operations), mount);
    if (mount->fuse == NULL) {
        VLOG_ERROR("containerv", "__vafs_mount: failed to create FUSE instance\n");
        vafs_close(mount->vafs);
        free(mount->mount_point);
        free(mount);
        return -1;
    }
    
    status = fuse_mount(mount->fuse, mount->mount_point);
    if (status != 0) {
        VLOG_ERROR("containerv", "__vafs_mount: failed to mount FUSE\n");
        fuse_destroy(mount->fuse);
        vafs_close(mount->vafs);
        free(mount->mount_point);
        free(mount);
        return -1;
    }
    
    status = thrd_create(&mount->worker, __fuse_loop_wrapper, (void*)mount->fuse);
    if (status != thrd_success) {
        VLOG_ERROR("containerv", "__vafs_mount: failed to create worker thread\n");
        fuse_unmount(mount->fuse);
        fuse_destroy(mount->fuse);
        vafs_close(mount->vafs);
        free(mount->mount_point);
        free(mount);
        return -1;
    }
    
    VLOG_DEBUG("containerv", "__vafs_mount: successfully mounted\n");
    *mount_out = mount;
    return 0;
}

static void __vafs_unmount(struct __vafs_mount* mount)
{
    if (mount == NULL) {
        return;
    }
    
    VLOG_DEBUG("containerv", "__vafs_unmount: unmounting %s\n", mount->mount_point);
    
    if (mount->worker != 0) {
        fuse_exit(mount->fuse);
        thrd_join(mount->worker, NULL);
        mount->worker = 0;
    }
    
    if (mount->fuse != NULL) {
        fuse_unmount(mount->fuse);
        fuse_destroy(mount->fuse);
    }
    
    if (mount->vafs != NULL) {
        vafs_close(mount->vafs);
    }
    
    free(mount->mount_point);
    free(mount);
}

// ============================================================================
// Layer Path Helpers
// ============================================================================

static void __containerv_layer_context_delete(struct containerv_layer_context* context)
{
    if (context == NULL) {
        return;
    }
    
    for (int i = 0; i < context->layer_count; i++) {
        struct __mounted_layer* layer = &context->layers[i];
        free(layer->mount_point);
        free(layer->source_path);
    }
    
    free(context->layers);
    free(context->work_dir);
    free(context->upper_dir);
    free(context->composed_rootfs);
    free(context->container_id);
    free(context);
}

static char* __create_layer_dir(const char* container_id, const char* subdir)
{
    char  tmp[PATH_MAX];
    char* path;
    
    snprintf(tmp, sizeof(tmp), "/var/chef/layers/%s/%s", container_id, subdir);
    path = strdup(tmp);
    if (path == NULL) {
        return NULL;
    }
    
    if (platform_mkdir(path) != 0 && errno != EEXIST) {
        VLOG_ERROR("containerv", "__create_layer_dir: failed to create %s\n", path);
        free(path);
        return NULL;
    }
    
    return path;
}

static struct containerv_layer_context* __containerv_layer_context_new(const char* containerID, size_t layerCount)
{
    struct containerv_layer_context* context;

    context = calloc(1, sizeof(struct containerv_layer_context));
    if (context == NULL) {
        return NULL;
    }
    
    context->container_id = strdup(containerID);
    context->layers = calloc(layerCount, sizeof(struct __mounted_layer));
    
    if (context->layers == NULL || context->container_id == NULL) {
        __containerv_layer_context_delete(context);
        return NULL;
    }

    // create the directories we always need
    context->upper_dir = __create_layer_dir(containerID, "contents");
    context->work_dir = __create_layer_dir(containerID, "workspace");
    context->composed_rootfs = __create_layer_dir(containerID, "merged");
    context->readonly = 1;
    
    if (context->work_dir == NULL || context->upper_dir == NULL || context->composed_rootfs == NULL) {
        __containerv_layer_context_delete(context);
        return NULL;
    }
    return context;
}

static char* __create_vafs_mount_point(const char* container_id, int layer_index)
{
    char tmp[64];
    snprintf(tmp, sizeof(tmp), "vafs-%d", layer_index);
    return __create_layer_dir(container_id, tmp);
}

// ============================================================================
// Layer Mounting
// ============================================================================

static int __setup_base_rootfs(
    struct containerv_layer* layer,
    const char*              container_id,
    struct __mounted_layer*  mountedLayer)
{
    VLOG_DEBUG("containerv", "__setup_base_rootfs: source=%s\n", 
               layer->source ? layer->source : "null");
    
    if (layer->source == NULL) {
        VLOG_ERROR("containerv", "__setup_base_rootfs: no source path\n");
        errno = EINVAL;
        return -1;
    }
    
    mountedLayer->handle = NULL;
    mountedLayer->type = layer->type;
    mountedLayer->mount_point = strdup(layer->source);
    mountedLayer->source_path = strdup(layer->source);
    if (mountedLayer->mount_point == NULL || mountedLayer->source_path == NULL) {
        free(mountedLayer->mount_point);
        free(mountedLayer->source_path);
        return -1;
    }

    return 0;
}

static char* __build_overlay_layer_list(
    struct containerv_layer_context* context,
    enum containerv_layer_type       skipType1,
    enum containerv_layer_type       skipType2)
{
    char*  dirs = NULL;
    size_t dirsLength = 0;

    for (int i = 0; i < context->layer_count; i++) {
        const char* layerPath;
        size_t      pathLength;

        if (context->layers[i].type == skipType1 || context->layers[i].type == skipType2) {
            continue;
        }
        
        layerPath = context->layers[i].mount_point;
        pathLength = strlen(layerPath);
        
        if (dirs == NULL) {
            dirsLength = pathLength + 1;
            dirs = malloc(dirsLength);
            if (dirs == NULL) {
                return NULL;
            }
            
            strcpy(dirs, layerPath);
        } else {
            size_t newDirsLength = dirsLength + pathLength + 2;
            char*  newDirs = realloc(dirs, newDirsLength);
            if (newDirs == NULL) {
                free(dirs);
                return NULL;
            }
            
            dirs = newDirs;
            strcat(dirs, ":");
            strcat(dirs, layerPath);
            dirsLength = newDirsLength;
        }
    }
    return dirs;
}

static int __create_overlay_mount(struct containerv_layer_context* context)
{
    char* options = NULL;
    char* lowerDirs = NULL;
    int   status;
    
    VLOG_DEBUG("containerv", "__create_overlay_mount: composing %d layers\n", context->layer_count);
    
    lowerDirs = __build_overlay_layer_list(context, CONTAINERV_LAYER_HOST_DIRECTORY, CONTAINERV_LAYER_OVERLAY);
    if (lowerDirs == NULL) {
        VLOG_ERROR("containerv", "__create_overlay_mount: no lower layers\n");
        errno = EINVAL;
        return -1;
    }
    
    if (context->readonly == 0) {
        size_t optSize = strlen("lowerdir=") + strlen(lowerDirs) + 
                    strlen(",upperdir=") + strlen(context->upper_dir) +
                    strlen(",workdir=") + strlen(context->work_dir) + 1;
        
        options = malloc(optSize);
        if (options == NULL) {
            free(lowerDirs);
            return -1;
        }
        
        snprintf(options, optSize, "lowerdir=%s,upperdir=%s,workdir=%s",
                lowerDirs, context->upper_dir, context->work_dir);
    } else {
        size_t optSize = strlen("lowerdir=") + strlen(lowerDirs) + 1;
        
        options = malloc(optSize);
        if (options == NULL) {
            free(lowerDirs);
            return -1;
        }
        snprintf(options, optSize, "lowerdir=%s", lowerDirs);
    }

    VLOG_DEBUG("containerv", "__create_overlay_mount: options=%s\n", options);
    VLOG_DEBUG("containerv", "__create_overlay_mount: target=%s\n", context->composed_rootfs);
    
    status = mount("overlay", context->composed_rootfs, "overlay", 0, options);
    if (status) {
        VLOG_ERROR("containerv", "__create_overlay_mount: mount failed: %s\n", strerror(errno));
        goto cleanup;
    }
    
    VLOG_DEBUG("containerv", "__create_overlay_mount: success\n");
    context->overlay_mounted = 1;

cleanup:
    free(options);
    free(lowerDirs);
    return status;
}

// ============================================================================
// Public API
// ============================================================================

int containerv_layers_mount_in_namespace(struct containerv_layer_context* context)
{
    int status;

    if (context == NULL) {
        errno = EINVAL;
        return -1;
    }

    VLOG_DEBUG("containerv", "containerv_layers_mount_in_namespace: %d layers for %s\n",
               context->layer_count, context->container_id);

    // 1) Mount all VAFS layers in this (current) mount namespace
    for (int i = 0; i < context->layer_count; ++i) {
        struct __mounted_layer* ml = &context->layers[i];

        if (ml->type != CONTAINERV_LAYER_VAFS_PACKAGE) {
            continue;
        }

        VLOG_DEBUG("containerv", "containerv_layers_mount_in_namespace: mounting VAFS %s at %s\n",
                   ml->source_path, ml->mount_point);

        struct __vafs_mount* mount_handle = NULL;
        status = __vafs_mount(ml->source_path, ml->mount_point, &mount_handle);
        if (status != 0) {
            VLOG_ERROR("containerv", "containerv_layers_mount_in_namespace: VAFS mount failed\n");
            return -1;
        }

        ml->handle = mount_handle;
    }

    // 2) Compose overlay in this namespace, if we have multiple layers
    if (context->layer_count > 1) {
        status = __create_overlay_mount(context);
        if (status != 0) {
            VLOG_ERROR("containerv", "containerv_layers_mount_in_namespace: overlay mount failed\n");
            // Optional: unmount already-mounted VAFS layers here
            return -1;
        }
    }

    // 3) Bind-mount any HOST_DIRECTORY layers into the composed rootfs.
    //    At this point, either:
    //      - composed_rootfs is the overlay mountpoint (multi-layer), or
    //      - composed_rootfs is a single-layer path (base or vafs).
    for (int i = 0; i < context->layer_count; ++i) {
        struct __mounted_layer* ml = &context->layers[i];

        if (ml->type != CONTAINERV_LAYER_HOST_DIRECTORY) {
            continue;
        }

        if (ml->source_path == NULL || ml->mount_point == NULL) {
            VLOG_ERROR("containerv", "containerv_layers_mount_in_namespace: HOST_DIRECTORY with missing paths\n");
            return -1;
        }

        // mount_point is a path inside the rootfs (e.g. /data), so we
        // need to combine composed_rootfs + mount_point to get an absolute
        // destination path in this mount namespace.
        char* destination = strpathcombine(context->composed_rootfs, ml->mount_point);
        if (destination == NULL) {
            return -1;
        }

        VLOG_DEBUG("containerv", "containerv_layers_mount_in_namespace: binding %s -> %s\n",
                   ml->source_path, destination);

        if (platform_mkdir(destination) != 0 && errno != EEXIST) {
            VLOG_ERROR("containerv", "containerv_layers_mount_in_namespace: failed to create %s\n", destination);
            free(destination);
            return -1;
        }

        status = mount(ml->source_path, destination, NULL, MS_BIND, NULL);
        if (status != 0) {
            VLOG_ERROR("containerv", "containerv_layers_mount_in_namespace: bind mount failed for %s -> %s: %s\n",
                       ml->source_path, destination, strerror(errno));
            free(destination);
            return -1;
        }

        free(destination);
    }

    return 0;
}

static int __process_context_layers(struct containerv_layer_context* context, struct containerv_layer* layers, int layer_count)
{
    int status = 0;

    for (int i = 0; i < layer_count; i++) {
        struct __mounted_layer* mounted_layer =
            &context->layers[context->layer_count];

        VLOG_DEBUG("containerv", "containerv_layers_compose: processing layer %d (type=%d)\n",
                   i, layers[i].type);

        switch (layers[i].type) {
            // CONTAINERV_LAYER_BASE_ROOTFS is meant to point to a directory path, which is already mounted.
            // CONTAINERV_LAYER_VAFS_PACKAGE is meant to point to a VaFS package file, which we mount via FUSE.
            // CONTAINERV_LAYER_HOST_DIRECTORY is a bind mount from host to container.
            // CONTAINERV_LAYER_OVERLAY is a writable overlay layer on top of the others. If not provided,
            // then we must mount the overlayfs as read-only.

            case CONTAINERV_LAYER_BASE_ROOTFS:
                // Just record base rootfs path; no mount here.
                status = __setup_base_rootfs(&layers[i], context->container_id, mounted_layer);
                break;

            case CONTAINERV_LAYER_VAFS_PACKAGE:
                // Plan the VaFS mount point, but don't call __vafs_mount yet.
                mounted_layer->type = layers[i].type;
                mounted_layer->source_path = layers[i].source ? strdup(layers[i].source) : NULL;
                mounted_layer->mount_point = __create_vafs_mount_point(context->container_id, i);
                if (mounted_layer->mount_point == NULL) {
                    status = -1;
                }
                break;

            case CONTAINERV_LAYER_HOST_DIRECTORY:
                mounted_layer->type = layers[i].type;
                mounted_layer->source_path = layers[i].source ? strdup(layers[i].source) : NULL;
                mounted_layer->mount_point = layers[i].target ? strdup(layers[i].target) : NULL;
                mounted_layer->readonly = layers[i].readonly;
                break;

            case CONTAINERV_LAYER_OVERLAY:
                mounted_layer->type = layers[i].type;
                context->readonly = 0;
                break;

            default:
                VLOG_ERROR(
                    "containerv",
                    "containerv_layers_compose: unknown layer type %d\n",
                    layers[i].type
                );
                status = -1;
                break;
        }

        if (status) {
            return status;
        }

        context->layer_count++;
    }

    return status;
}

int containerv_layers_compose_ex(
    struct containerv_layer*          layers,
    int                               layerCount,
    const char*                       containerID,
    const struct containerv_layers_compose_options* compose_options,
    struct containerv_layer_context** contextOut)
{
    (void)compose_options;
    struct containerv_layer_context* context;
    int                              status = 0;
    
    VLOG_DEBUG("containerv", "containerv_layers_compose: %d layers for %s\n", 
               layerCount, containerID);
    
    if (layers == NULL || layerCount == 0 || containerID == NULL || contextOut == NULL) {
        errno = EINVAL;
        return -1;
    }
    
    context = __containerv_layer_context_new(containerID, layerCount);
    if (context == NULL) {
        return -1;
    }

    status = __process_context_layers(context, layers, layerCount);
    if (status) {
        containerv_layers_destroy(context);
        return status;
    }

    // Decide composed_rootfs **path only**, but don't mount overlay here.
    if (context->layer_count == 1 && context->layers[0].type != CONTAINERV_LAYER_OVERLAY) {
        // Single concrete layer - use its mount_point path directly as the rootfs.
        free(context->composed_rootfs);
        context->composed_rootfs = strdup(context->layers[0].mount_point);
        if (context->composed_rootfs == NULL) {
            containerv_layers_destroy(context);
            return -1;
        }
    }
    // else: for multiple layers we keep context->composed_rootfs as
    //       /var/chef/layers/<id>/merged which will be mounted later
    
    VLOG_DEBUG("containerv", 
        "containerv_layers_compose: complete, rootfs=%s\n", 
        context->composed_rootfs
    );
    
    *contextOut = context;
    return 0;
}

int containerv_layers_compose(
    struct containerv_layer*          layers,
    int                               layer_count,
    const char*                       container_id,
    struct containerv_layer_context** context_out)
{
    return containerv_layers_compose_ex(layers, layer_count, container_id, NULL, context_out);
}

int containerv_layers_compose_with_options(
    struct containerv_layer*          layers,
    int                               layer_count,
    const char*                       container_id,
    const struct containerv_options*  options,
    struct containerv_layer_context** context_out)
{
    (void)options;
    return containerv_layers_compose_ex(layers, layer_count, container_id, NULL, context_out);
}

const char* containerv_layers_get_rootfs(struct containerv_layer_context* context)
{
    if (context == NULL) {
        return NULL;
    }
    return context->composed_rootfs;
}

int containerv_layers_iterate(
    struct containerv_layer_context* context,
    enum containerv_layer_type       layerType,
    containerv_layers_iterate_cb     cb,
    void*                            userContext)
{
    if (context == NULL || cb == NULL) {
        errno = EINVAL;
        return -1;
    }

    for (int i = 0; i < context->layer_count; ++i) {
        struct __mounted_layer* ml = &context->layers[i];
        if (ml->type != layerType) {
            continue;
        }

        if (ml->source_path == NULL || ml->mount_point == NULL) {
            errno = EINVAL;
            return -1;
        }

        int status = cb(ml->source_path, ml->mount_point, ml->readonly, userContext);
        if (status != 0) {
            return status;
        }
    }

    return 0;
}

void containerv_layers_destroy(struct containerv_layer_context* context)
{
    if (context == NULL) {
        return;
    }

    VLOG_DEBUG("containerv", "containerv_layers_destroy: cleaning up %d layers\n", 
               context->layer_count);

    // Unmount overlay if it exists
    if (context->overlay_mounted && context->composed_rootfs != NULL) {
        umount2(context->composed_rootfs, MNT_DETACH);
    }

    // Unmount all the layers
    for (int i = 0; i < context->layer_count; i++) {
        struct __mounted_layer* layer = &context->layers[i];
        if (layer->type == CONTAINERV_LAYER_VAFS_PACKAGE && layer->handle != NULL) {
            __vafs_unmount((struct __vafs_mount*)layer->handle);
        }
    }
    
    __containerv_layer_context_delete(context);
}
