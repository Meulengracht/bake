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
 * CVD owns VaFS FUSE servers so their lifetime is independent of a container
 * child. Mounts are created before containerv_create() forks, which lets the
 * child inherit them when it unshares its mount namespace. The manager keys
 * entries by package path and reference-counts them across containers.
 */
#define FUSE_USE_VERSION 32

#include "vafs_manager.h"

#include <chef/list.h>
#include <chef/platform.h>
#include <chef/vafs.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fuse3/fuse.h>
#include <inttypes.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <threads.h>
#include <vafs/reader.h>
#include <vafs/stat.h>
#include <vlog.h>

#define __VAFS_MOUNT_ROOT "/var/chef/vafs"

struct cvd_vafs_mount {
    struct list_item                item_header;
    char*                           package_path;
    char*                           mount_point;
    struct VaFs*                    vafs;
    struct chef_vafs_codec_context* codec_context;
    struct fuse*                    fuse;
    thrd_t                          worker;
    int                             worker_started;
    int                             mounted;
    unsigned int                    ref_count;
};

/**
 * One reference from a container instance to a shared package mount. 
 */
struct cvd_vafs_layer_ref {
    struct cvd_vafs_mount* mount;
    int                    layer_index;
};

/** 
 * All shared package mounts acquired while preparing one container.
 */
struct cvd_vafs_layer_instance {
    struct cvd_vafs_layer_ref* refs;
    int                        ref_count;
};

static struct {
    struct list mounts;
    mtx_t       lock;
    int         initialized;
} g_vafs_mount_manager = { 0 };

// FUSE callback implementation backed by the read-only VaFS object reader.
static int __vafs_getattr(const char* path, struct stat* stbuf, struct fuse_file_info* fi)
{
    struct fuse_context*      context = fuse_get_context();
    struct cvd_vafs_mount*    mount = (struct cvd_vafs_mount*)context->private_data;
    struct VaFsObjectReader*  handle = NULL;
    struct VaFsMetadata       metadata;
    int                       status;
    int                       isRoot;

    memset(stbuf, 0, sizeof(struct stat));
    if (fi != NULL && fi->fh != 0) {
        handle = (struct VaFsObjectReader*)fi->fh;
        status = vafs_object_reader_stat(handle, &metadata);
        if (status != 0) {
            return -errno;
        }

        stbuf->st_blksize = 512;
        stbuf->st_mode = metadata.Mode;
        stbuf->st_size = (off_t)metadata.Size;
        stbuf->st_nlink = metadata.LinkCount ? metadata.LinkCount : 1;
        return 0;
    }

    status = vafs_object_reader_open(mount->vafs, path, VaFsLookup_NoFollow, &handle);
    if (status) {
        return -errno;
    }

    status = vafs_object_reader_stat(handle, &metadata);
    vafs_object_reader_close(handle);
    if (status != 0) {
        return -errno;
    }

    isRoot = (strcmp(path, "/") == 0);
    stbuf->st_blksize = 512;
    stbuf->st_mode = metadata.Mode;
    stbuf->st_size = (off_t)metadata.Size;
    stbuf->st_nlink = isRoot ? 2 : (metadata.LinkCount ? metadata.LinkCount : 1);
    return 0;
}

static int __vafs_open(const char* path, struct fuse_file_info* fi)
{
    struct fuse_context*      context = fuse_get_context();
    struct cvd_vafs_mount*    mount = (struct cvd_vafs_mount*)context->private_data;
    struct VaFsObjectReader*  handle;
    int                       status;

    if ((fi->flags & O_ACCMODE) != O_RDONLY) {
        return -EACCES;
    }

    status = vafs_object_reader_open(mount->vafs, path, VaFsLookup_None, &handle);
    if (status) {
        return -errno;
    }

    fi->fh = (uint64_t)handle;
    return 0;
}

static int __vafs_read(const char* path, char* buf, size_t size, off_t offset, struct fuse_file_info* fi)
{
    struct VaFsObjectReader* handle = (struct VaFsObjectReader*)fi->fh;
    uint64_t                 bytesRead;
    int                      status;

    (void)path;

    if (handle == NULL) {
        return -EINVAL;
    }

    if (offset != 0) {
        status = vafs_object_reader_seek(handle, offset, SEEK_SET);
        if (status) {
            return -errno;
        }
    }

    bytesRead = vafs_object_reader_read(handle, buf, size);
    if (bytesRead == UINT64_MAX) {
        return -errno;
    }
    return (int)bytesRead;
}

static int __vafs_release(const char* path, struct fuse_file_info* fi)
{
    struct VaFsObjectReader* handle = (struct VaFsObjectReader*)fi->fh;

    (void)path;

    if (handle == NULL) {
        return -EINVAL;
    }

    vafs_object_reader_close(handle);
    fi->fh = 0;
    return 0;
}

static int __vafs_opendir(const char* path, struct fuse_file_info* fi)
{
    struct fuse_context*         context = fuse_get_context();
    struct cvd_vafs_mount*       mount = (struct cvd_vafs_mount*)context->private_data;
    struct VaFsDirectoryReader*  handle;
    int                          status;

    status = vafs_directory_reader_open(mount->vafs, path, VaFsLookup_None, &handle);
    if (status) {
        return -errno;
    }

    fi->fh = (uint64_t)handle;
    return 0;
}

static int __vafs_readdir(const char* path, void* buf, fuse_fill_dir_t filler,
                          off_t offset, struct fuse_file_info* fi, enum fuse_readdir_flags flags)
{
    struct VaFsDirectoryReader* handle = (struct VaFsDirectoryReader*)fi->fh;
    int                         status;

    (void)path;
    (void)offset;
    (void)flags;

    if (handle == NULL) {
        return -EINVAL;
    }

    filler(buf, ".", NULL, 0, 0);
    filler(buf, "..", NULL, 0, 0);

    while (1) {
        struct VaFsEntry entry;

        status = vafs_directory_reader_next(handle, &entry);
        if (status != 0) {
            if (errno != ENOENT) {
                return -errno;
            }
            break;
        }

        status = filler(buf, entry.Name, NULL, 0, 0);
        if (status != 0) {
            break;
        }
    }

    return 0;
}

static int __vafs_releasedir(const char* path, struct fuse_file_info* fi)
{
    struct VaFsDirectoryReader* handle = (struct VaFsDirectoryReader*)fi->fh;

    (void)path;

    if (handle == NULL) {
        return -EINVAL;
    }

    vafs_directory_reader_close(handle);
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

// Run the blocking FUSE event loop outside the CVD request thread.
static int __fuse_loop_wrapper(void* arg)
{
    struct fuse* fuse = (struct fuse*)arg;
    return fuse_loop(fuse);
}

static uint64_t __hash_path(const char* path)
{
    uint64_t hash = 1469598103934665603ULL;

    while (*path != '\0') {
        hash ^= (unsigned char)*path;
        hash *= 1099511628211ULL;
        path++;
    }
    return hash;
}

/**
 * Build the stable daemon-visible mount point for a package.
 *
 * The hash is only a directory-name optimization; mount reuse is still
 * decided by the full package path under the manager lock.
 */
static char* __create_mount_point(const char* package_path)
{
    char path[PATH_MAX];
    int  status;

    status = snprintf(
        &path[0],
        sizeof(path),
        __VAFS_MOUNT_ROOT "/%016" PRIx64,
        __hash_path(package_path)
    );
    if (status < 0 || status >= (int)sizeof(path)) {
        errno = ENAMETOOLONG;
        return NULL;
    }

    if (platform_mkdir(&path[0]) != 0) {
        VLOG_ERROR("cvd", "vafs_mount_manager: failed to create %s\n", path);
        return NULL;
    }
    return platform_strdup(&path[0]);
}

static struct cvd_vafs_mount* __find_mount_locked(const char* package_path)
{
    struct list_item* item;

    list_foreach(&g_vafs_mount_manager.mounts, item) {
        struct cvd_vafs_mount* mount = (struct cvd_vafs_mount*)item;
        if (strcmp(mount->package_path, package_path) == 0) {
            return mount;
        }
    }
    return NULL;
}

// Start the reader, FUSE mount, and worker for a newly allocated entry.
static int __vafs_mount_start(struct cvd_vafs_mount* mount)
{
    char*            argv[] = { "cvd-vafs" };
    struct fuse_args args = FUSE_ARGS_INIT(1, argv);
    int              status;

    status = chef_vafs_codec_context_create(&mount->codec_context);
    if (status != 0) {
        VLOG_ERROR("cvd", "vafs_mount_manager: failed to initialize compression context\n");
        return -1;
    }

    status = chef_vafs_reader_open_file(mount->codec_context, mount->package_path, &mount->vafs);
    if (status != 0) {
        VLOG_ERROR("cvd", "vafs_mount_manager: failed to open VaFS package %s\n", mount->package_path);
        return -1;
    }

    mount->fuse = fuse_new(&args, &g_vafs_operations, sizeof(g_vafs_operations), mount);
    if (mount->fuse == NULL) {
        VLOG_ERROR("cvd", "vafs_mount_manager: failed to create FUSE instance for %s\n", mount->package_path);
        return -1;
    }

    status = fuse_mount(mount->fuse, mount->mount_point);
    if (status != 0) {
        VLOG_ERROR("cvd", "vafs_mount_manager: failed to mount %s at %s\n",
                   mount->package_path, mount->mount_point);
        return -1;
    }
    mount->mounted = 1;

    status = thrd_create(&mount->worker, __fuse_loop_wrapper, (void*)mount->fuse);
    if (status != thrd_success) {
        VLOG_ERROR("cvd", "vafs_mount_manager: failed to create FUSE worker for %s\n", mount->package_path);
        fuse_unmount(mount->fuse);
        mount->mounted = 0;
        return -1;
    }
    mount->worker_started = 1;
    return 0;
}

static void __vafs_mount_stop(struct cvd_vafs_mount* mount)
{
    if (mount == NULL) {
        return;
    }

    if (mount->package_path != NULL && mount->mount_point != NULL) {
        VLOG_DEBUG("cvd", "vafs_mount_manager: stopping %s at %s\n",
                   mount->package_path, mount->mount_point);
    }

    if (mount->fuse != NULL) {
        fuse_exit(mount->fuse);
    }

    if (mount->fuse != NULL && mount->mounted) {
        fuse_unmount(mount->fuse);
        mount->mounted = 0;
    }

    if (mount->worker_started) {
        thrd_join(mount->worker, NULL);
        mount->worker_started = 0;
    }

    if (mount->fuse != NULL) {
        fuse_destroy(mount->fuse);
    }

    if (mount->vafs != NULL) {
        vafs_reader_close(mount->vafs);
    }
    chef_vafs_codec_context_destroy(mount->codec_context);

    if (mount->mount_point != NULL) {
        platform_rmdir(mount->mount_point);
    }

    free(mount->mount_point);
    free(mount->package_path);
    free(mount);
}

// Allocate and fully start a mount entry; clean up partial setup on failure.
static int __vafs_mount_new(const char* package_path, struct cvd_vafs_mount** mount_out)
{
    struct cvd_vafs_mount* mount;
    int                    status;

    mount = calloc(1, sizeof(struct cvd_vafs_mount));
    if (mount == NULL) {
        return -1;
    }

    mount->package_path = platform_strdup(package_path);
    mount->mount_point = __create_mount_point(package_path);
    mount->ref_count = 1;
    if (mount->package_path == NULL || mount->mount_point == NULL) {
        __vafs_mount_stop(mount);
        return -1;
    }

    VLOG_DEBUG("cvd", "vafs_mount_manager: mounting %s at %s\n",
               mount->package_path, mount->mount_point);

    status = __vafs_mount_start(mount);
    if (status != 0) {
        __vafs_mount_stop(mount);
        return -1;
    }

    *mount_out = mount;
    return 0;
}

// Acquire a shared mount. The caller must hold g_vafs_mount_manager.lock.
static int __acquire_mount_locked(const char* package_path, struct cvd_vafs_mount** mount_out)
{
    struct cvd_vafs_mount* mount;
    int                    status;

    mount = __find_mount_locked(package_path);
    if (mount != NULL) {
        mount->ref_count++;
        *mount_out = mount;
        VLOG_DEBUG("cvd", "vafs_mount_manager: reusing %s (refs=%u)\n",
                   package_path, mount->ref_count);
        return 0;
    }

    status = __vafs_mount_new(package_path, &mount);
    if (status != 0) {
        return status;
    }

    list_add(&g_vafs_mount_manager.mounts, &mount->item_header);
    *mount_out = mount;
    return 0;
}

// Release one shared mount reference. The caller must hold the manager lock.
static void __release_mount_locked(struct cvd_vafs_mount* mount)
{
    if (mount == NULL) {
        return;
    }

    if (mount->ref_count > 1) {
        mount->ref_count--;
        VLOG_DEBUG("cvd", "vafs_mount_manager: released %s (refs=%u)\n",
                   mount->package_path, mount->ref_count);
        return;
    }

    list_remove(&g_vafs_mount_manager.mounts, &mount->item_header);
    __vafs_mount_stop(mount);
}

/**
 * Drop mounts left behind by a previous cvd that exited without unmounting.
 *
 * These live in the daemon's mount namespace, so they outlive the process and
 * would otherwise accumulate as unreferenced FUSE mounts across restarts.
 */
static void __reclaim_stale_mounts(void)
{
    struct dirent* entry;
    DIR*           directory;

    directory = opendir(__VAFS_MOUNT_ROOT);
    if (directory == NULL) {
        return;
    }

    while ((entry = readdir(directory)) != NULL) {
        char path[PATH_MAX];
        int  written;

        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        written = snprintf(&path[0], sizeof(path), "%s/%s", __VAFS_MOUNT_ROOT, entry->d_name);
        if (written < 0 || written >= (int)sizeof(path)) {
            continue;
        }

        VLOG_WARNING("cvd", "vafs_manager: reclaiming stale mount %s\n", &path[0]);
        if (umount2(&path[0], MNT_DETACH) != 0 && errno != EINVAL && errno != ENOENT) {
            VLOG_ERROR("cvd", "vafs_manager: failed to unmount %s: %s\n", &path[0], strerror(errno));
        }
        platform_rmdir(&path[0]);
    }
    closedir(directory);
}

/**
 * Initialize the manager state before any request can acquire a mount.
 * mtx_plain is sufficient because all manager operations use one short
 * critical section and never require recursive locking.
 */
int cvd_vafs_mount_manager_initialize(void)
{
    if (g_vafs_mount_manager.initialized) {
        return 0;
    }

    list_init(&g_vafs_mount_manager.mounts);
    if (mtx_init(&g_vafs_mount_manager.lock, mtx_plain) != thrd_success) {
        VLOG_ERROR("cvd", "vafs_manager: failed to initialize lock\n");
        return -1;
    }

    __reclaim_stale_mounts();

    g_vafs_mount_manager.initialized = 1;
    return 0;
}

/**
 * Final cleanup used during daemon shutdown. The safe list walk permits each
 * entry to be removed before its FUSE resources are synchronously stopped.
 */
void cvd_vafs_mount_manager_shutdown(void)
{
    struct list_item* item;
    struct list_item* next;

    if (!g_vafs_mount_manager.initialized) {
        return;
    }

    mtx_lock(&g_vafs_mount_manager.lock);
    list_foreach_safe(&g_vafs_mount_manager.mounts, item, next) {
        struct cvd_vafs_mount* mount = (struct cvd_vafs_mount*)item;
        VLOG_ERROR("cvd", "vafs_mount_manager: forcing shutdown of %s with %u refs\n",
                   mount->package_path, mount->ref_count);
        list_remove(&g_vafs_mount_manager.mounts, &mount->item_header);
        __vafs_mount_stop(mount);
    }
    mtx_unlock(&g_vafs_mount_manager.lock);

    mtx_destroy(&g_vafs_mount_manager.lock);
    g_vafs_mount_manager.initialized = 0;
}

/**
 * Prepare all VaFS layers for one container while holding references until
 * container teardown. Mutating the descriptors here avoids making containerv
 * aware of package files or FUSE implementation details.
 */
int cvd_vafs_mount_manager_prepare_layers(
    struct containerv_layer*          layers,
    int                               layerCount,
    struct cvd_vafs_layer_instance**  instanceOut)
{
    struct cvd_vafs_layer_instance* instance = NULL;
    int                             vafsCount = 0;
    int                             status = 0;

    if (instanceOut == NULL) {
        errno = EINVAL;
        return -1;
    }
    *instanceOut = NULL;

    if (layers == NULL || layerCount < 0) {
        errno = EINVAL;
        return -1;
    }

    if (!g_vafs_mount_manager.initialized && cvd_vafs_mount_manager_initialize() != 0) {
        return -1;
    }

    for (int i = 0; i < layerCount; ++i) {
        if (layers[i].type == CONTAINERV_LAYER_VAFS_PACKAGE) {
            vafsCount++;
        }
    }
    if (vafsCount == 0) {
        return 0;
    }

    instance = calloc(1, sizeof(struct cvd_vafs_layer_instance));
    if (instance == NULL) {
        return -1;
    }

    instance->refs = calloc((size_t)vafsCount, sizeof(struct cvd_vafs_layer_ref));
    if (instance->refs == NULL) {
        free(instance);
        return -1;
    }

    // Keep acquisition and rollback atomic with respect to other containers.
    mtx_lock(&g_vafs_mount_manager.lock);
    for (int i = 0; i < layerCount; ++i) {
        struct cvd_vafs_mount* mount;

        if (layers[i].type != CONTAINERV_LAYER_VAFS_PACKAGE) {
            continue;
        }

        if (layers[i].source == NULL || layers[i].source[0] == '\0') {
            VLOG_ERROR("cvd", "vafs_mount_manager: VaFS layer is missing package path\n");
            errno = EINVAL;
            status = -1;
            break;
        }

        status = __acquire_mount_locked(layers[i].source, &mount);
        if (status != 0) {
            break;
        }

        instance->refs[instance->ref_count].mount = mount;
        instance->refs[instance->ref_count].layer_index = i;
        instance->ref_count++;
    }

    if (status != 0) {
        // A partial prepare must not leak references.
        for (int i = instance->ref_count - 1; i >= 0; --i) {
            __release_mount_locked(instance->refs[i].mount);
        }
        instance->ref_count = 0;
    } else {
        // Rewrite only once every acquisition succeeded; rolling back after a
        // partial rewrite would leave descriptors pointing at a released mount.
        for (int i = 0; i < instance->ref_count; ++i) {
            struct containerv_layer* layer = &layers[instance->refs[i].layer_index];

            // CVD owns package instance. containerv only sees a stable mounted
            // lowerdir, inherited by the child when it unshares its mount namespace.
            layer->type = CONTAINERV_LAYER_BASE_ROOTFS;
            layer->source = instance->refs[i].mount->mount_point;
            layer->target = NULL;
            layer->readonly = 1;
        }
    }
    mtx_unlock(&g_vafs_mount_manager.lock);

    if (status != 0) {
        cvd_vafs_layer_instance_release(instance);
        return status;
    }

    *instanceOut = instance;
    return 0;
}

// Release an instance in reverse acquisition order.
void cvd_vafs_layer_instance_release(struct cvd_vafs_layer_instance* instance)
{
    if (instance == NULL) {
        return;
    }

    if (g_vafs_mount_manager.initialized) {
        mtx_lock(&g_vafs_mount_manager.lock);
        for (int i = instance->ref_count - 1; i >= 0; --i) {
            __release_mount_locked(instance->refs[i].mount);
        }
        mtx_unlock(&g_vafs_mount_manager.lock);
    }

    free(instance->refs);
    free(instance);
}