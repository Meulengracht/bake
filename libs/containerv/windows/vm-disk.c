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
 */

#include <chef/platform.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vlog.h>

#include <windows.h>
#include <shlwapi.h>
#include <virtdisk.h>

#include "private.h"

#ifndef CREATE_VIRTUAL_DISK_VERSION_1
#define CREATE_VIRTUAL_DISK_VERSION_1 1
#endif
#ifndef CREATE_VIRTUAL_DISK_PARAMETERS_DEFAULT_VERSION
#define CREATE_VIRTUAL_DISK_PARAMETERS_DEFAULT_VERSION CREATE_VIRTUAL_DISK_VERSION_1
#endif

#define WINDOWS_DEFAULT_VM_DISK_MB 4096

static void __spawn_output_handler(const char* line, enum platform_spawn_output_type type)
{
    if (type == PLATFORM_SPAWN_OUTPUT_TYPE_STDOUT) {
        VLOG_DEBUG("containerv[vhdx]", "%s", line);
    } else {
        VLOG_WARNING("containerv[vhdx]", "%s", line);
    }
}

static char* __ps_escape_single_quotes_alloc(const char* s)
{
    if (s == NULL) {
        return NULL;
    }

    size_t extra = 0;
    for (const char* p = s; *p; ++p) {
        if (*p == '\'') {
            extra++;
        }
    }

    size_t n = strlen(s);
    char* out = calloc(n + extra + 1, 1);
    if (out == NULL) {
        return NULL;
    }

    size_t j = 0;
    for (size_t i = 0; i < n; ++i) {
        out[j++] = s[i];
        if (s[i] == '\'') {
            out[j++] = '\'';
        }
    }
    out[j] = '\0';
    return out;
}

static int __windows_create_vhdx(const char* vhdx_path, uint64_t size_mb)
{
    if (vhdx_path == NULL || vhdx_path[0] == '\0') {
        errno = EINVAL;
        return -1;
    }

    wchar_t vhd_path_w[MAX_PATH];
    if (MultiByteToWideChar(CP_UTF8, 0, vhdx_path, -1, vhd_path_w, MAX_PATH) == 0) {
        errno = EINVAL;
        return -1;
    }

    VIRTUAL_STORAGE_TYPE vst;
    memset(&vst, 0, sizeof(vst));
    vst.DeviceId = VIRTUAL_STORAGE_TYPE_DEVICE_VHDX;
    vst.VendorId = VIRTUAL_STORAGE_TYPE_VENDOR_MICROSOFT;

    CREATE_VIRTUAL_DISK_PARAMETERS params;
    memset(&params, 0, sizeof(params));
    params.Version = CREATE_VIRTUAL_DISK_PARAMETERS_DEFAULT_VERSION;
    params.Version1.MaximumSize = (ULONGLONG)size_mb * 1024ULL * 1024ULL;
    params.Version1.BlockSizeInBytes = 0;
    params.Version1.SectorSizeInBytes = 0;

    HANDLE h = INVALID_HANDLE_VALUE;
    DWORD result = CreateVirtualDisk(
        &vst,
        vhd_path_w,
        VIRTUAL_DISK_ACCESS_ALL,
        NULL,
        CREATE_VIRTUAL_DISK_FLAG_NONE,
        0,
        &params,
        NULL,
        &h);

    if (result != ERROR_SUCCESS) {
        VLOG_ERROR("containerv[vhdx]", "CreateVirtualDisk failed (%lu) for %s\n", result, vhdx_path);
        errno = EIO;
        return -1;
    }

    CloseHandle(h);
    return 0;
}

static int __windows_populate_vhdx_ntfs(const char* vhdx_path, const char* src_dir)
{
    if (vhdx_path == NULL || src_dir == NULL) {
        errno = EINVAL;
        return -1;
    }

    // Do everything in one PowerShell run so we don't need to parse drive letters.
    char* vhdx_esc = __ps_escape_single_quotes_alloc(vhdx_path);
    char* src_esc = __ps_escape_single_quotes_alloc(src_dir);
    if (vhdx_esc == NULL || src_esc == NULL) {
        free(vhdx_esc);
        free(src_esc);
        errno = ENOMEM;
        return -1;
    }

    // Use robocopy for speed and decent semantics. Note: robocopy exit codes are special.
    // We treat 0..7 as success (copy ok / some extra files), >7 as failure.
    char ps[8192];
    int n = snprintf(
        ps,
        sizeof(ps),
        "-NoProfile -NonInteractive -ExecutionPolicy Bypass -Command "
        "\"$ErrorActionPreference='Stop'; "
        "$v='%s'; $s='%s'; "
        "$m=Mount-VHD -Path $v -PassThru; "
        "$d=$m | Get-Disk; "
        "if ($d.PartitionStyle -eq 'RAW') { Initialize-Disk -Number $d.Number -PartitionStyle GPT -PassThru | Out-Null }; "
        "$p=(Get-Partition -DiskNumber $d.Number -ErrorAction SilentlyContinue | Where-Object { $_.Type -ne 'Reserved' } | Select-Object -First 1); "
        "if (-not $p) { $p=New-Partition -DiskNumber $d.Number -UseMaximumSize -AssignDriveLetter }; "
        "$v2=(Get-Volume -Partition $p -ErrorAction SilentlyContinue); "
        "if (-not $v2 -or -not $v2.FileSystem) { Format-Volume -Partition $p -FileSystem NTFS -NewFileSystemLabel 'chef' -Confirm:$false | Out-Null }; "
        "$dl=$p.DriveLetter; if (-not $dl) { throw 'no drive letter assigned' }; "
        "$dst=($dl + ':\\'); "
        "$rc=robocopy $s $dst /E /COPY:DAT /DCOPY:DAT /R:2 /W:1 /NFL /NDL /NJH /NJS /NP; "
        "if ($LASTEXITCODE -gt 7) { throw ('robocopy failed ' + $LASTEXITCODE) }; "
        "Dismount-VHD -Path $v;\"",
        vhdx_esc,
        src_esc);

    free(vhdx_esc);
    free(src_esc);

    if (n < 0 || (size_t)n >= sizeof(ps)) {
        errno = EINVAL;
        return -1;
    }

    int status = platform_spawn(
        "powershell.exe",
        ps,
        NULL,
        &(struct platform_spawn_options){
            .output_handler = __spawn_output_handler,
        });

    if (status != 0) {
        VLOG_ERROR("containerv[vhdx]", "failed to populate VHDX via PowerShell (status=%d)\n", status);
        errno = EIO;
        return -1;
    }

    return 0;
}

int __windows_prepare_vm_disk(struct containerv_container* container, const struct containerv_options* options)
{
    if (container == NULL || container->runtime_dir == NULL || container->runtime_dir[0] == '\0') {
        errno = EINVAL;
        return -1;
    }

    // Target disk path used by HCS VM config.
    char dst_vhdx[MAX_PATH];
    int rc = snprintf(dst_vhdx, sizeof(dst_vhdx), "%s\\container.vhdx", container->runtime_dir);
    if (rc < 0 || (size_t)rc >= sizeof(dst_vhdx)) {
        errno = EINVAL;
        return -1;
    }

    if (PathFileExistsA(dst_vhdx)) {
        VLOG_DEBUG("containerv[vhdx]", "container VHDX already exists: %s\n", dst_vhdx);
        return 0;
    }

    const char* src_root = container->rootfs;
    if (src_root == NULL || src_root[0] == '\0') {
        VLOG_ERROR("containerv[vhdx]", "missing composed rootfs path\n");
        errno = EINVAL;
        return -1;
    }

    // Fast path (B1): prebuilt bootable VHDX shipped inside the rootfs materialization.
    char src_vhdx[MAX_PATH];
    rc = snprintf(src_vhdx, sizeof(src_vhdx), "%s\\container.vhdx", src_root);
    if (rc > 0 && (size_t)rc < sizeof(src_vhdx) && PathFileExistsA(src_vhdx)) {
        VLOG_DEBUG("containerv[vhdx]", "using prebuilt guest VHDX from %s\n", src_vhdx);
        if (platform_copyfile(src_vhdx, dst_vhdx) != 0) {
            VLOG_ERROR("containerv[vhdx]", "failed to copy prebuilt VHDX to %s\n", dst_vhdx);
            errno = EIO;
            return -1;
        }
        return 0;
    }

    // Otherwise: create a simple NTFS VHDX and copy the materialized rootfs tree into it.
    // NOTE: For Linux guests this implies your init/boot must support mounting an NTFS root.
    // If/when we standardize on ext4, this should be replaced with a WSL-backed ext4 format path.
    uint64_t size_mb = WINDOWS_DEFAULT_VM_DISK_MB;
    (void)options;

    VLOG_DEBUG("containerv[vhdx]", "creating container VHDX at %s (%llu MB)\n", dst_vhdx, (unsigned long long)size_mb);
    if (__windows_create_vhdx(dst_vhdx, size_mb) != 0) {
        return -1;
    }

    VLOG_DEBUG("containerv[vhdx]", "populating container VHDX from %s\n", src_root);
    if (__windows_populate_vhdx_ntfs(dst_vhdx, src_root) != 0) {
        DeleteFileA(dst_vhdx);
        return -1;
    }

    return 0;
}
