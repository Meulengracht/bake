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

static char* __windows_base_archive(const char* base) {
    char  tmp[1024];
    char* variant = strchr(base, ':');
    
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
 * @brief Download the Windows base image to the specified cache directory.
 * @param path The directory to construct the rootfs into.
 * @param base The base image string, e.g., "windows:ltsc2022".
 * @return 0 on success, non-zero on failure.
 */
extern int containerv_disk_setup_windows_image(const char* path, const char* base);

#endif // !__CONTAINERV_DISK_WINDOWS_H__
