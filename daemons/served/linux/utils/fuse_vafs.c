/**
 * Copyright 2022, Philip Meulengracht
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
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <vafs/vafs.h>

struct served_mount {
    struct VaFs* vafs;
    struct fuse* fuse;
    const char*  mount_point;
};

static int __find_file_recursive(
    struct VaFsDirectoryHandle* directory,
    const char*                 path,
    struct VaFsFileHandle**     handleOut)
{
    struct VaFsDirectoryHandle* subdirectoryHandle;
    int                         status;
    char*                       token;
    char*                       remainingPath;

    // extract next token from the remaining path
    remainingPath = strchr(path, '/');
    if (!remainingPath) {
        return vafs_directory_open_file(directory, path, handleOut);
    }

    token = strndup(path, remainingPath - path);
    if (!token) {
        return -1;
    }

    status = vafs_directory_open_directory(directory, token, &subdirectoryHandle);
    free(token);
    if (status != 0) {
        return status;
    }

    status = __find_file_recursive(subdirectoryHandle, remainingPath, handleOut);
    vafs_directory_close(subdirectoryHandle);
    return 0;
}

static int __find_file(struct VaFs* vafs, const char* path, struct VaFsFileHandle** handleOut)
{
    struct VaFsDirectoryHandle* root;
    int                         status;

    status = vafs_directory_open(vafs, "/", &root);
    if (status != 0) {
        return -1;
    }

    status = __find_file_recursive(root, path, handleOut);
    vafs_directory_close(root);
    return 0;
}

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

    if (__find_file(mount->vafs, path, &handle) != 0) {
        return -1;
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
    struct VaFsFileHandle*      filehandle;
    struct VaFsDirectoryHandle* dirhandle;
    int                         status;

    status = __find_file(mount->vafs, path, &filehandle);
    if (status != 0 && errno != ENFILE) {
        return -1;
    }

    if (status == 0) {
        unsigned int perms = vafs_file_permissions(filehandle);
        unsigned int req   = permissions;
        if ((req & perms) != req) {
            errno = EACCES;
            return -1;
        }
        return 0;
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

    if (handle == NULL) {
        errno = EINVAL;
        return -1;
    }
    
    if (vafs_file_read(handle, buffer, count)) {
        return -1;
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
    struct VaFsFileHandle* handle = (struct VaFsFileHandle*)fi->fh;

    if (handle == NULL) {
        errno = EINVAL;
        return -1;
    }
    
    memset(stat, 0, sizeof(struct stat));
    stat->st_blksize = 512;
    stat->st_mode = vafs_file_permissions(handle);
    stat->st_size = (off_t)vafs_file_length(handle);
    return 0;
}

/**
 * Find next data or hole after the specified offset
 */
off_t __vafs_lseek(const char* path, off_t off, int whence, struct fuse_file_info* fi)
{
    struct fuse_context*   context = fuse_get_context();
    struct served_mount*   mount   = (struct served_mount*)context->private_data;
    struct VaFsFileHandle* handle  = (struct VaFsFileHandle*)fi->fh;

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

    if (handle == NULL) {
        errno = EINVAL;
        return -1;
    }    
    return vafs_file_close(handle);
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
    struct fuse_context* context = fuse_get_context();
    struct served_mount* mount   = (struct served_mount*)context->private_data;

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

    if (handle == NULL) {
        errno = EINVAL;
        return -1;
    }
    

}

/** Release directory
 */
int __vafs_releasedir(const char* path, struct fuse_file_info* fi)
{
    struct fuse_context*        context = fuse_get_context();
    struct served_mount*        mount   = (struct served_mount*)context->private_data;
    struct VaFsDirectoryHandle* handle  = (struct VaFsDirectoryHandle*)fi->fh;

    if (handle == NULL) {
        errno = EINVAL;
        return -1;
    }
    

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

int served_mount(const char* path, const char* mountPoint, struct served_mount** mountOut)
{
    struct fuse_args     args;
    struct served_mount* mount;
    int                  status;

    if (path == NULL || mountPoint == NULL || mountOut == NULL) {
        errno = EINVAL;
        return -1;
    }

    mount = served_mount_new(mountPoint);
    if (mount == NULL) {
        return -1;
    }

    status = vafs_open_file(path, &mount->vafs);
    if (status != 0) {
        served_mount_delete(mount);
        return -1;
    }

    status = __prepare_fuse_args(&args);
    if (status != 0) {
        served_mount_delete(mount);
        return -1;
    }

    mount->fuse = fuse_new(&args, &g_vafsOperations, sizeof(g_vafsOperations), mount);
    if (mount->fuse == NULL) {
        served_mount_delete(mount);
        return -1;
    }
    
    status = fuse_mount(mount->fuse, mount->mount_point);
    if (status != 0) {
        served_mount_delete(mount);
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

    fuse_unmount(mount->fuse);
    served_mount_delete(mount);
}
