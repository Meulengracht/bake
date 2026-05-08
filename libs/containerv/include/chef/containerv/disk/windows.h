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
 * Rootfs helpers for Windows-based container disks.
 */

#ifndef __CONTAINERV_DISK_WINDOWS_H__
#define __CONTAINERV_DISK_WINDOWS_H__

#include <chef/platform.h>
#include <stdlib.h>
#include <string.h>
#include <vlog.h>

/**
 * @brief Build the default downloadable WCOW base archive URL for a selector.
 * @param base Base selector such as "windows:ltsc2022". NULL defaults to ltsc2022.
 * @return Allocated URL string, or NULL on failure.
 */
static char* __resolve_windows_wcow_base_url(const char* base) {
    char  tmp[1024];
    const char* variant = base != NULL ? strchr(base, ':') : NULL;
    
    if (variant == NULL) {
        variant = "ltsc2022";
    } else {
        variant += 1;
    }

    snprintf(&tmp[0], sizeof(tmp), 
        "https://chef-store-eu-basic.s3.de.io.cloud.ovh.net/build-bases/windows-%s-%s.tar.xz",
        variant,
        CHEF_ARCHITECTURE_STR
    );
    return platform_strdup(&tmp[0]);
}

/**
 * @brief Build the default downloadable LCOW UVM bundle URL.
 * @return Allocated URL string, or NULL on failure.
 */
static char* __resolve_windows_lcow_base_url(void) {
    char tmp[1024];
    snprintf(&tmp[0], sizeof(tmp), 
        "https://chef-store-eu-basic.s3.de.io.cloud.ovh.net/build-bases/windows-lcow-%s.tar.xz",
        CHEF_ARCHITECTURE_STR
    );
    return platform_strdup(&tmp[0]);
}

/**
 * @brief Resolve a cached WCOW base archive and unpack it into the requested path.
 * @param path Destination directory for the unpacked Windows base.
 * @param base Base image string, e.g., "windows:ltsc2022".
 * @return 0 on success, non-zero on failure.
 */
extern int containerv_disk_setup_wcow_uvm(const char* path, const char* base);

/**
 * @brief Validate that the provided directory is a usable LCOW UVM bundle.
 *
 * Supported bundle layouts are:
 * - legacy: a top-level `uvm.vhdx`/`uvm.vhd`
 * - boot files: a kernel (`kernel` or `vmlinux`) plus either `initrd(.img)`
 *   or `rootfs.vhd(.x)`
 */
extern int containerv_disk_validate_lcow_uvm(const char* image_path);

/**
 * @brief Detect optional LCOW bundle files under the provided image path.
 *
 * When present, the returned strings are allocated and must be freed by the
 * caller. Missing optional files return NULL outputs.
 */
extern int containerv_disk_lcow_detect_uvm_files(
    const char* image_path,
    char**      kernel_file_out,
    char**      initrd_file_out,
    char**      boot_parameters_out);

struct containerv_disk_lcow_uvm_config {
    /** Optional override URL for the LCOW UVM archive. */
    const char* uvm_url;
};

/**
 * @brief Resolve (download/cache) LCOW UVM assets and return a shared cached bundle path.
 *
 * The unpacked bundle is reusable across containers; callers should treat the
 * returned directory as read-only shared cache state.
 *
 * If config is NULL or config->uvm_url is empty, the default bundled LCOW URL is used.
 *
 * On success, allocates a string in *uvmImageOut which must be freed
 * by the caller.
 */
extern int containerv_disk_setup_lcow_uvm(
    const struct containerv_disk_lcow_uvm_config* config,
    char**                                        uvmImageOut);

#endif // !__CONTAINERV_DISK_WINDOWS_H__
