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
 * VaFs Builder
 * - Contains the implementation of the VaFs.
 *   This filesystem is used to store the initrd of the kernel.
 */

#define FUSE_USE_VERSION 32

#include <errno.h>
#include <fuse3/fuse.h>
#include <threads.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <vafs/vafs.h>
#include <vafs/file.h>
#include <vafs/symlink.h>
#include <vafs/directory.h>
#include <vafs/stat.h>
#include <utils.h>
#include <vlog.h>
#include <zstd.h>

struct served_mount {
    struct VaFs* vafs;
    struct fuse* fuse;
    const char*  mount_point;
    thrd_t       worker;
};

/** Open a file
 *
 * Open flags are available in fi->flags. The following rules
 * apply.
 *
 *  - Creation (O_CREAT, O_EXCL, O_NOCTTY) flags will be
 *    filtered out / handled by the kernel.
 *
 *  - Access modes (O_RDONLY, O_WRONLY, O_RDWR, O_EXEC, O_SEARCH)
 *    should be used by the filesystem to check if the operation is
 *    permitted.  If the ``-o default_permissions`` mount option is
 *    given, this check is already done by the kernel before calling
 *    open() and may thus be omitted by the filesystem.
 *
 *  - When writeback caching is enabled, the kernel may send
 *    read requests even for files opened with O_WRONLY. The
 *    filesystem should be prepared to handle this.
 *
 *  - When writeback caching is disabled, the filesystem is
 *    expected to properly handle the O_APPEND flag and ensure
 *    that each write is appending to the end of the file.
 * 
     *  - When writeback caching is enabled, the kernel will
 *    handle O_APPEND. However, unless all changes to the file
 *    come through the kernel this will not work reliably. The
 *    filesystem should thus either ignore the O_APPEND flag
 *    (and let the kernel handle it), or return an error
 *    (indicating that reliably O_APPEND is not available).
 *
 * Filesystem may store an arbitrary file handle (pointer,
 * index, etc) in fi->fh, and use this in other all other file
 * operations (read, write, flush, release, fsync).
 *
 * Filesystem may also implement stateless file I/O and not store
 * anything in fi->fh.
 *
 * There are also some flags (direct_io, keep_cache) which the
 * filesystem may set in fi, to change the way the file is opened.
 * See fuse_file_info structure in <fuse_common.h> for more details.
 *
 * If this request is answered with an error code of ENOSYS
 * and FUSE_CAP_NO_OPEN_SUPPORT is set in
 * `fuse_conn_info.capable`, this is treated as success and
 * future calls to open will also succeed without being send
 * to the filesystem process.
 *
 */
int __vafs_open(const char* path, struct fuse_file_info* fi)
{
    struct fuse_context*   context = fuse_get_context();
    struct served_mount*   mount   = (struct served_mount*)context->private_data;
    struct VaFsFileHandle* handle;
    int                    status;
    VLOG_DEBUG("fuse", "open(path=%s)\n", path);

	if ((fi->flags & O_ACCMODE) != O_RDONLY) {
        errno = EACCES;
		return -1;
    }

    status = vafs_file_open(mount->vafs, path, &handle);
    if (status) {
        return status;
    }

    fi->fh = (uint64_t)handle;
    return 0;
}

/**
 * Check file access permissions
 *
 * This will be called for the access() system call.  If the
 * 'default_permissions' mount option is given, this method is not
 * called.
 *
 * This method is not called under Linux kernel versions 2.4.x
 */
int __vafs_access(const char* path, int permissions)
{
    struct fuse_context* context = fuse_get_context();
    struct served_mount* mount   = (struct served_mount*)context->private_data;
    struct vafs_stat     stat;
    int                  status;
    VLOG_DEBUG("fuse", "access(path=%s, perms=%i)\n", path, permissions);

    status = vafs_path_stat(mount->vafs, path, 1, &stat);
    if (status) {
        return status;
    }

    if ((stat.mode & (uint32_t)permissions) != (uint32_t)permissions) {
        errno = EACCES;
        return -1;
    }
    return 0;
}

/** Read data from an open file
 *
 * Read should return exactly the number of bytes requested except
 * on EOF or error, otherwise the rest of the data will be
 * substituted with zeroes.	 An exception to this is when the
 * 'direct_io' mount option is specified, in which case the return
 * value of the read system call will reflect the return value of
 * this operation.
 */
int __vafs_read(const char* path, char* buffer, size_t count, off_t offset, struct fuse_file_info* fi)
{
    struct fuse_context*   context = fuse_get_context();
    struct served_mount*   mount   = (struct served_mount*)context->private_data;
    struct VaFsFileHandle* handle  = (struct VaFsFileHandle*)fi->fh;
    int                    status;
    VLOG_DEBUG("fuse", "read(path=%s, count=%zu, offset=%li)\n", path, count, offset);

    if (handle == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (offset != 0) {
        status = vafs_file_seek(handle, offset, SEEK_SET);
        if (status) {
            return status;
        }
    }

    status = (int)vafs_file_read(handle, buffer, count);
    if (status) {
        return status;
    }
    return (int)count;
}

/** Get file attributes.
 *
 * Similar to stat().  The 'st_dev' and 'st_blksize' fields are
 * ignored. The 'st_ino' field is ignored except if the 'use_ino'
 * mount option is given. In that case it is passed to userspace,
 * but libfuse and the kernel will still assign a different
 * inode for internal use (called the "nodeid").
 *
 * `fi` will always be NULL if the file is not currently open, but
 * may also be NULL if the file is open.
 */
int __vafs_getattr(const char* path, struct stat* stat, struct fuse_file_info *fi)
{
    struct fuse_context* context  = fuse_get_context();
    struct served_mount* mount    = (struct served_mount*)context->private_data;
    struct vafs_stat     vstat;
    int                  status;
    int                  isRoot;
    VLOG_DEBUG("fuse", "getattr(path=%s)\n", path);

    memset(stat, 0, sizeof(struct stat));
    if (fi != NULL && fi->fh != 0) {
        struct VaFsFileHandle* handle = (struct VaFsFileHandle*)fi->fh;

        stat->st_blksize = 512;
        stat->st_mode    = vafs_file_permissions(handle);
        stat->st_size    = (off_t)vafs_file_length(handle);
        stat->st_nlink   = 1;
        return 0;
    }

    status = vafs_path_stat(mount->vafs, path, 0, &vstat);
    if (status) {
        return status;
    }

    // root has 2 links
    isRoot = (strcmp(path, "/") == 0);

    stat->st_blksize = 512;
    stat->st_mode    = vstat.mode;
    stat->st_size    = (off_t)vstat.size;
    stat->st_nlink   = isRoot + 1;
    return 0;
}

/** Read the target of a symbolic link
 *
 * The buffer should be filled with a null terminated string.  The
 * buffer size argument includes the space for the terminating
 * null character.	If the linkname is too long to fit in the
 * buffer, it should be truncated.	The return value should be 0
 * for success.
 */
int __vafs_readlink(const char* path, char* linkBuffer, size_t bufferSize)
{
    struct fuse_context*      context  = fuse_get_context();
    struct served_mount*      mount    = (struct served_mount*)context->private_data;
    struct VaFsSymlinkHandle* handle;
    int                       status;
    VLOG_DEBUG("fuse", "readlink(path=%s)\n", path);

    status = vafs_symlink_open(mount->vafs, path, &handle);
    if (status) {
        return status;
    }

    status = vafs_symlink_target(handle, linkBuffer, bufferSize);
    vafs_symlink_close(handle);
    return status;
}

/**
 * Find next data or hole after the specified offset
 */
off_t __vafs_lseek(const char* path, off_t off, int whence, struct fuse_file_info* fi)
{
    struct fuse_context*   context = fuse_get_context();
    struct served_mount*   mount   = (struct served_mount*)context->private_data;
    struct VaFsFileHandle* handle  = (struct VaFsFileHandle*)fi->fh;
    VLOG_DEBUG("fuse", "lseek(path=%s)\n", path);

    if (handle == NULL) {
        errno = EINVAL;
        return -1;
    }
    
    vafs_file_seek(handle, off, whence);
    return off;
}

/** Release an open file
 *
 * Release is called when there are no more references to an open
 * file: all file descriptors are closed and all memory mappings
 * are unmapped.
 *
 * For every open() call there will be exactly one release() call
 * with the same flags and file handle.  It is possible to
 * have a file opened more than once, in which case only the last
 * release will mean, that no more reads/writes will happen on the
 * file.  The return value of release is ignored.
 */
int __vafs_release(const char* path, struct fuse_file_info* fi)
{
    struct fuse_context*   context = fuse_get_context();
    struct served_mount*   mount   = (struct served_mount*)context->private_data;
    struct VaFsFileHandle* handle  = (struct VaFsFileHandle*)fi->fh;
    int                    status;
    VLOG_DEBUG("fuse", "release(path=%s)\n", path);

    if (handle == NULL) {
        errno = EINVAL;
        return -1;
    }

    status = vafs_file_close(handle);
    fi->fh = 0;
    return status;
}

/** Open directory
 *
 * Unless the 'default_permissions' mount option is given,
 * this method should check if opendir is permitted for this
 * directory. Optionally opendir may also return an arbitrary
 * filehandle in the fuse_file_info structure, which will be
 * passed to readdir, releasedir and fsyncdir.
 */
int __vafs_opendir(const char* path, struct fuse_file_info* fi)
{
    struct fuse_context*        context = fuse_get_context();
    struct served_mount*        mount   = (struct served_mount*)context->private_data;
    struct VaFsDirectoryHandle* handle;
    int                         status;
    VLOG_DEBUG("fuse", "opendir(path=%s)\n", path);

    status = vafs_directory_open(mount->vafs, path, &handle);
    if (status) {
        return status;
    }

    fi->fh = (uint64_t)handle;
    return 0;
}

/** Read directory
 *
 * The filesystem may choose between two modes of operation:
 *
 * 1) The readdir implementation ignores the offset parameter, and
 * passes zero to the filler function's offset.  The filler
 * function will not return '1' (unless an error happens), so the
 * whole directory is read in a single readdir operation.
 *
 * 2) The readdir implementation keeps track of the offsets of the
 * directory entries.  It uses the offset parameter and always
 * passes non-zero offset to the filler function.  When the buffer
 * is full (or an error happens) the filler function will return
 * '1'.
 */
int __vafs_readdir(
    const char*             path,
    void*                   buffer,
    fuse_fill_dir_t         fill,
    off_t                   offset,
    struct fuse_file_info*  fi,
    enum fuse_readdir_flags flags)
{
    struct fuse_context*        context = fuse_get_context();
    struct served_mount*        mount   = (struct served_mount*)context->private_data;
    struct VaFsDirectoryHandle* handle  = (struct VaFsDirectoryHandle*)fi->fh;
    int                         status;
    VLOG_DEBUG("fuse", "readdir(path=%s)\n", path);

    if (handle == NULL) {
        errno = EINVAL;
        return -1;
    }

    // add ./.. 
	fill(buffer, ".", NULL, 0, 0);
	fill(buffer, "..", NULL, 0, 0);

    while (1) {
        struct VaFsEntry entry;
        //struct stat      stat;
        status = vafs_directory_read(handle, &entry);
        if (status) {
            if (errno != ENOENT) {
                return status;
            }
            break;
        }

        // TODO support retrieving file attributes from a directory entry
        /*if (flags & FUSE_READDIR_PLUS) {
            status = fill(buffer, entry.Name, &stat, 0, FUSE_FILL_DIR_PLUS);
            if (status) {
                return status;
            }
        } else { */
            status = fill(buffer, entry.Name, NULL, 0, 0);
            if (status) {
                return status;
            }
        //}
    }

    return 0;
}

/** Release directory
 */
int __vafs_releasedir(const char* path, struct fuse_file_info* fi)
{
    struct fuse_context*        context = fuse_get_context();
    struct served_mount*        mount   = (struct served_mount*)context->private_data;
    struct VaFsDirectoryHandle* handle  = (struct VaFsDirectoryHandle*)fi->fh;
    int                         status;
    VLOG_DEBUG("fuse", "releasedir(path=%s)\n", path);

    if (handle == NULL) {
        errno = EINVAL;

	if ((fi->flags & O_ACCMODE) != O_RDONLY)
		return -EACCES;

        return -1;
    }

    status = vafs_directory_close(handle);
    fi->fh = 0;
    return status;
}

/** Get file system statistics
 *
 * The 'f_favail', 'f_fsid' and 'f_flag' fields are ignored
 */
int __vafs_statfs(const char* path, struct statvfs* stat)
{
    struct fuse_context* context = fuse_get_context();
    struct served_mount* mount   = (struct served_mount*)context->private_data;

    // not used
    (void)path;

    stat->f_bsize   = 512; // block size? how are we doing this?
    stat->f_frsize  = 512; // fragment size, dunno
    stat->f_blocks  = 0; // eh, we have no way of determining this atm
    stat->f_bfree   = 0; // Always zero
    stat->f_bavail  = 0; // Always zero
    stat->f_ffree   = 0; // Always zero
    stat->f_favail  = 0; // Always zero
    stat->f_files   = 0; // ...
    stat->f_fsid    = 0; // ignored?
    stat->f_flag    = ST_RDONLY; // ignored?
    stat->f_namemax = 255;
    return 0;
}

static struct VaFsGuid g_filterGuid    = VA_FS_FEATURE_FILTER;
static struct VaFsGuid g_filterOpsGuid = VA_FS_FEATURE_FILTER_OPS;

static int __zstd_decode(void* Input, uint32_t InputLength, void* Output, uint32_t* OutputLength)
{
    /* Read the content size from the frame header. For simplicity we require
     * that it is always present. By default, zstd will write the content size
     * in the header when it is known. If you can't guarantee that the frame
     * content size is always written into the header, either use streaming
     * decompression, or ZSTD_decompressBound().
     */
    size_t             decompressedSize;
    unsigned long long contentSize = ZSTD_getFrameContentSize(Input, InputLength);
    if (contentSize == ZSTD_CONTENTSIZE_ERROR || contentSize == ZSTD_CONTENTSIZE_UNKNOWN) {
        fprintf(stderr, "__zstd_decode: failed to get frame content size\n");
        return -1;
    }
    
    /* Decompress.
     * If you are doing many decompressions, you may want to reuse the context
     * and use ZSTD_decompressDCtx(). If you want to set advanced parameters,
     * use ZSTD_DCtx_setParameter().
     */
    decompressedSize = ZSTD_decompress(Output, *OutputLength, Input, InputLength);
    if (ZSTD_isError(decompressedSize)) {
        return -1;
    }
    *OutputLength = (uint32_t)decompressedSize;
    return 0;
}

static int __set_filter_ops(
    struct VaFs* vafs)
{
    struct VaFsFeatureFilterOps filterOps;

    memcpy(&filterOps.Header.Guid, &g_filterOpsGuid, sizeof(struct VaFsGuid));

    filterOps.Header.Length = sizeof(struct VaFsFeatureFilterOps);
    filterOps.Encode = NULL;
    filterOps.Decode = __zstd_decode;

    return vafs_feature_add(vafs, &filterOps.Header);
}

static int __handle_filter(struct VaFs* vafs)
{
    struct VaFsFeatureFilter* filter;
    int                       status;

    status = vafs_feature_query(vafs, &g_filterGuid, (struct VaFsFeatureHeader**)&filter);
    if (status) {
        // no filter present
        return 0;
    }
    return __set_filter_ops(vafs);
}

/**
 * All methods are optional, but some are essential for a useful
 * filesystem (e.g. getattr).  Open, flush, release, fsync, opendir,
 * releasedir, fsyncdir, access, create, truncate, lock, init and
 * destroy are special purpose methods, without which a full featured
 * filesystem can still be implemented.
 */
static struct fuse_operations g_vafsOperations = {
    .open       = __vafs_open,
    .access     = __vafs_access,
    .read       = __vafs_read,
    .lseek      = __vafs_lseek,
    .getattr    = __vafs_getattr,
    .readlink   = __vafs_readlink,
    .release    = __vafs_release,
    .opendir    = __vafs_opendir,
    .readdir    = __vafs_readdir,
    .releasedir = __vafs_releasedir,
    .statfs     = __vafs_statfs,
};

static struct served_mount* served_mount_new(const char* mountPoint)
{
    struct served_mount* mount = (struct served_mount*)malloc(sizeof(struct served_mount));
    if (mount == NULL) {
        return NULL;
    }

    mount->mount_point = mountPoint != NULL ? strdup(mountPoint) : NULL;
    mount->fuse        = NULL;
    mount->vafs        = NULL;
    mount->worker      = 0;

    return mount;
}

static void served_mount_delete(struct served_mount* mount)
{
    if (mount == NULL) {
        return;
    }

    if (mount->vafs != NULL) {
        vafs_close(mount->vafs);
    }

    if (mount->fuse != NULL) {
        fuse_destroy(mount->fuse);
    }

    free((void*)mount->mount_point);
    free(mount);
}

static int __prepare_fuse_args(struct fuse_args* args)
{
    args->allocated = 1;
    args->argc = 1;
    args->argv = (char**)calloc(sizeof(char*), args->argc);
    if (args->argv == NULL) {
        return -1;
    }

    args->argv[0] = strdup("/bin/served");
    if (args->argv[0] == NULL) {
        free(args->argv);
        return -1;
    }
    args->argv[1] = NULL;
    return 0;
}

static int __reset_mountpoint(struct served_mount* mount)
{
    char* command;
    int   status;

    command = (char*)malloc(strlen(mount->mount_point) + 64);
    if (command == NULL) {
        return -1;
    }

    sprintf(&command[0], "umount -l %s", mount->mount_point);
    status = system(&command[0]);
    free(command);
    return 0;
}

// Wrapper function for fuse_loop to match thrd_start_t signature
static int __fuse_loop_wrapper(void* arg)
{
    struct fuse* fuse = (struct fuse*)arg;
    return fuse_loop(fuse);
}

int served_mount(const char* path, const char* mountPoint, struct served_mount** mountOut)
{
    struct fuse_args     args;
    struct served_mount* mount;
    int                  status;
    VLOG_DEBUG("fuse", "served_mount(path=%s, mountPoint=%s)\n", path, mountPoint);

    if (path == NULL || mountPoint == NULL || mountOut == NULL) {
        errno = EINVAL;
        return -1;
    }

    mount = served_mount_new(mountPoint);
    if (mount == NULL) {
        VLOG_ERROR("fuse", "failed to create mount data\n");
        return -1;
    }

    status = vafs_open_file(path, &mount->vafs);
    if (status != 0) {
        VLOG_ERROR("fuse", "failed to open vafs image\n");
        served_mount_delete(mount);
        return -1;
    }

    status = __handle_filter(mount->vafs);
    if (status) {
        VLOG_ERROR("fuse", "failed to set decode filter for vafs image\n");
        served_mount_delete(mount);
        return -1;
    }

    status = __prepare_fuse_args(&args);
    if (status != 0) {
        VLOG_ERROR("fuse", "failed to prepare fuse\n");
        served_mount_delete(mount);
        return -1;
    }

    mount->fuse = fuse_new(&args, &g_vafsOperations, sizeof(g_vafsOperations), mount);
    if (mount->fuse == NULL) {
        VLOG_ERROR("fuse", "failed to create a new fuse instance\n");
        served_mount_delete(mount);
        return -1;
    }
    
    status = fuse_mount(mount->fuse, mount->mount_point);
    if (status != 0) {
        // so we might receive ENOTCONN here, which means 'Transport endpoint is not connected'
        // which means that the mount was left mounted due to a bad shutdown, lets unmount it first
        //system("umount -l mount->mount_point")
        if (errno = ENOTCONN) {
            VLOG_DEBUG("fuse", "fuse_mount returned ENOTCONN, trying to unmount first\n");
            status = __reset_mountpoint(mount);
            if (status == 0) {
                VLOG_DEBUG("fuse", "successfully unmounted, now retrying mount\n");
                // now try again, the unmount should have worked
                status = fuse_mount(mount->fuse, mount->mount_point);
            }
        }
        
        if (status) {
            VLOG_ERROR("fuse", "failed to mount fuse at %s\n", mount->mount_point);
            served_mount_delete(mount);
            return -1;
        }
    }

    status = thrd_create(&mount->worker, __fuse_loop_wrapper, (void*)mount->fuse);
    if (status != thrd_success) {
        served_unmount(mount);
        return -1;
    }

    *mountOut = mount;
    return 0;
}

void served_unmount(struct served_mount* mount)
{
    if (mount == NULL) {
        return;
    }

    // kill the worker thread
    if (mount->worker != 0) {
        VLOG_DEBUG("fuse", "killing fuse worker thread\n");
        // Signal the fuse loop to exit
        fuse_exit(mount->fuse);
        // Wait for the thread to finish
        thrd_join(mount->worker, NULL);
        mount->worker = 0;
        VLOG_DEBUG("fuse", "fuse worker thread killed\n");
    }

    fuse_unmount(mount->fuse);
    served_mount_delete(mount);
}
