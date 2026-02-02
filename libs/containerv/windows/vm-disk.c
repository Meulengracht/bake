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
#include <stdlib.h>
#include <string.h>
#include <vlog.h>

#include <windows.h>
#include <shlwapi.h>

#include "private.h"

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

    // Windows guests require a bootable OS disk image. A plain NTFS data volume populated
    // from a directory tree is not sufficient for UEFI boot (no ESP/BCD).
    if (container->guest_is_windows != 0) {
        VLOG_ERROR(
            "containerv[vhdx]",
            "Windows guest requires a bootable disk; expected %s\\container.vhdx (prebuilt)\n",
            src_root
        );
        errno = ENOENT;
        return -1;
    }

    // Linux guest path: WSL2 import stores the guest filesystem as ext4.vhdx in the rootfs directory.
    // Prefer copying that instead of generating an NTFS VHDX (many Linux init setups will not boot from NTFS-root).
    // (No directory-tree -> ext4 disk generation is implemented here.)
    (void)options;
    char wsl_ext4[MAX_PATH];
    rc = snprintf(wsl_ext4, sizeof(wsl_ext4), "%s\\ext4.vhdx", src_root);
    if (rc > 0 && (size_t)rc < sizeof(wsl_ext4) && PathFileExistsA(wsl_ext4)) {
        VLOG_DEBUG("containerv[vhdx]", "using WSL ext4.vhdx as guest disk from %s\n", wsl_ext4);
        if (platform_copyfile(wsl_ext4, dst_vhdx) != 0) {
            VLOG_ERROR("containerv[vhdx]", "failed to copy ext4.vhdx to %s\n", dst_vhdx);
            errno = EIO;
            return -1;
        }
        return 0;
    }

    VLOG_ERROR(
        "containerv[vhdx]",
        "Linux guest requires a bootable disk; expected %s\\container.vhdx (prebuilt) or %s\\ext4.vhdx (WSL2 import)\n",
        src_root,
        src_root
    );
    errno = ENOENT;
    return -1;
}
