/** Copyright, Philip Meulengracht. Licensed under GPL-3.0-or-later. */
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <chef/platform.h>
#include <vlog.h>

#include "private.h"
#include <initguid.h>
#include <virtdisk.h>
#include "lcow-storage-script.h"

int __lcow_storage_copy_seed(const char* source, const char* destination)
{
    struct list files;
    struct list_item* item;
    DWORD attributes = GetFileAttributesA(source);
    int status;
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        DWORD error = GetLastError();
        errno = (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) ? ENOENT : EIO;
        return -1;
    }
    if (!(attributes & FILE_ATTRIBUTE_DIRECTORY) || (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
        errno = (attributes & FILE_ATTRIBUTE_REPARSE_POINT) ? ENOTSUP : ENOTDIR;
        return -1;
    }
    list_init(&files);
    if (platform_mkdir(destination) != 0) {
        return -1;
    }
    status = platform_getfiles(source, 0, &files);
    if (status != 0) {
        platform_getfiles_destroy(&files);
        return status;
    }
    list_foreach(&files, item) {
        struct platform_file_entry* entry = (struct platform_file_entry*)item;
        char* target;
        DWORD attributes;
        // Cached disks are runtime state, never part of an image/package layer.
        if (_stricmp(entry->name, ".containerv-lcow") == 0) {
            continue;
        }
        attributes = GetFileAttributesA(entry->path);
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
            errno = attributes == INVALID_FILE_ATTRIBUTES ? EIO : ENOTSUP;
            status = -1;
            break;
        }
        target = strpathcombine(destination, entry->name);
        if (target == NULL) {
            status = -1;
            break;
        }
        if (entry->type == PLATFORM_FILETYPE_DIRECTORY) {
            status = platform_copydir(entry->path, target);
        } else if (entry->type == PLATFORM_FILETYPE_FILE) {
            status = platform_copyfile(entry->path, target);
        } else {
            errno = ENOTSUP;
            status = -1;
        }
        free(target);
        if (status != 0) break;
    }
    platform_getfiles_destroy(&files);
    return status;
}

int __lcow_storage_prepare(struct containerv_container* container)
{
    VIRTUAL_STORAGE_TYPE type = { VIRTUAL_STORAGE_TYPE_DEVICE_VHDX, VIRTUAL_STORAGE_TYPE_VENDOR_MICROSOFT };
    CREATE_VIRTUAL_DISK_PARAMETERS parameters = {0};
    HANDLE disk = NULL;
    char* directory = strpathcombine(container->rootfs, ".containerv-lcow");
    char* lock_path = NULL;
    wchar_t* disk_path = NULL;
    DWORD error;
    int status = -1;

    if (directory == NULL || platform_mkdir(directory) != 0) {
        goto cleanup;
    }
    container->lcow_disk_path = strpathcombine(directory, "rootfs.vhdx");
    container->lcow_disk_pending = strpathcombine(directory, "format.pending");
    lock_path = strpathcombine(directory, "lock");
    if (container->lcow_disk_path == NULL || container->lcow_disk_pending == NULL || lock_path == NULL) {
        goto cleanup;
    }
    // The cached rootfs owns this disk. Serialize its use across CVD processes
    // as well as containers, and retain the lock until HCS has released it.
    container->lcow_disk_lock = CreateFileA(lock_path, GENERIC_READ | GENERIC_WRITE,
        0, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (container->lcow_disk_lock == INVALID_HANDLE_VALUE) {
        container->lcow_disk_lock = NULL;
        VLOG_ERROR("containerv", "LCOW rootfs is locked or inaccessible: %s (error=%lu)\n", directory, GetLastError());
        errno = EBUSY;
        goto cleanup;
    }
    if (GetFileAttributesA(container->lcow_disk_path) == INVALID_FILE_ATTRIBUTES) {
        error = GetLastError();
        if (error != ERROR_FILE_NOT_FOUND) {
            errno = EIO;
            goto cleanup;
        }
        disk_path = __windows_utf8_to_wide_alloc(container->lcow_disk_path);
        if (disk_path == NULL) {
            goto cleanup;
        }
        parameters.Version = CREATE_VIRTUAL_DISK_VERSION_2;
        parameters.Version2.MaximumSize = 64ULL * 1024 * 1024 * 1024;
        parameters.Version2.BlockSizeInBytes = 2 * 1024 * 1024;
        parameters.Version2.SectorSizeInBytes = 512;
        parameters.Version2.PhysicalSectorSizeInBytes = 4096;
        // Record ownership of an unfinished initialization before creating the
        // disk. An interruption must not turn a new disk into a published one.
        disk = CreateFileA(container->lcow_disk_pending, GENERIC_WRITE, 0, NULL,
            OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (disk == INVALID_HANDLE_VALUE) {
            errno = EIO;
            goto cleanup;
        }
        CloseHandle(disk);
        error = CreateVirtualDisk(&type, disk_path, VIRTUAL_DISK_ACCESS_NONE, NULL,
            CREATE_VIRTUAL_DISK_FLAG_NONE, 0, &parameters, NULL, &disk);
        if (error != ERROR_SUCCESS) {
            VLOG_ERROR("containerv", "failed to create LCOW disk %s (error=%lu)\n", container->lcow_disk_path, error);
            errno = EIO;
            goto cleanup;
        }
        CloseHandle(disk);
    }
    // This marker exists only until the first successful initialization, before
    // any workload is allowed to run. It permits retrying an interrupted seed.
    container->lcow_disk_fresh = GetFileAttributesA(container->lcow_disk_pending) != INVALID_FILE_ATTRIBUTES;
    if (windows_grant_vm_group_access(container->lcow_disk_path) != 0) {
        goto cleanup;
    }
    status = 0;
cleanup:
    free(directory);
    free(lock_path);
    free(disk_path);
    return status;
}

int __lcow_storage_mount(struct containerv_container* container)
{
    const char* argv[] = {
        "/bin/busybox", "timeout", "300", "/bin/sh", "-c",
        "exec >/dev/kmsg 2>&1; exec /bin/sh -c \"$1\" chef-lcow-storage \"$2\" \"$3\"",
        "chef-lcow-storage", containerv_lcow_storage_script,
        container->lcow_disk_fresh ? "1" : "0", HCS_LCOW_STORAGE_CONTROLLER, NULL
    };
    struct containerv_lcow_gcs_process process = {0};
    unsigned long exit_code = 0;

    if (__lcow_gcs_execute_host_process(container, argv, &process.pid) != 0) {
        return -1;
    }
    process.magic = CONTAINERV_LCOW_GCS_PROCESS_MAGIC;
    snprintf(process.container_id, sizeof(process.container_id), "00000000-0000-0000-0000-000000000000");
    if (__hcs_wait_lcow_gcs_process(container, &process, &exit_code) != 0 || exit_code != 0) {
        VLOG_ERROR("containerv", "LCOW native storage setup failed (exit=%lu); see UVM console log. Disk preserved: %s\n",
            exit_code, container->lcow_disk_path);
        errno = EIO;
        return -1;
    }
    if (container->lcow_disk_fresh && !DeleteFileA(container->lcow_disk_pending)) {
        VLOG_ERROR("containerv", "failed to commit LCOW disk initialization: %lu\n", GetLastError());
        errno = EIO;
        return -1;
    }
    container->lcow_storage_ready = 1;
    VLOG_DEBUG("containerv", "LCOW rootfs uses persistent ext4 storage: %s\n", container->lcow_disk_path);
    return 0;
}

void __lcow_storage_flush(struct containerv_container* container)
{
    const char* argv[] = { "/bin/busybox", "timeout", "30", "/bin/busybox", "sync", NULL };
    struct containerv_lcow_gcs_process process = {0};
    unsigned long exit_code = 0;
    if (!container->lcow_storage_ready) {
        return;
    }
    process.magic = CONTAINERV_LCOW_GCS_PROCESS_MAGIC;
    snprintf(process.container_id, sizeof(process.container_id), "00000000-0000-0000-0000-000000000000");
    if (__lcow_gcs_execute_host_process(container, argv, &process.pid) != 0 ||
        __hcs_wait_lcow_gcs_process(container, &process, &exit_code) != 0 || exit_code != 0) {
        VLOG_WARNING("containerv", "failed to flush LCOW disk before shutdown\n");
    }
}
