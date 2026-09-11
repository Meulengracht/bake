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
 * Rootfs helpers for Ubuntu-based container disks.
 */

#ifndef __CONTAINERV_DISK_UBUNTU_H__
#define __CONTAINERV_DISK_UBUNTU_H__

#include <chef/platform.h>
#include <stdlib.h>
#include <string.h>
#include <vlog.h>

/**
 * @brief Parse the Ubuntu major version from a base selector.
 * @param base Base selector such as "ubuntu:24".
 * @return Parsed major version, defaulting to 24 when base is NULL.
 */
static int __ubuntu_get_base_number(const char* base) {
    const char* p;
    int         version;
    
    if (base == NULL) {
        return 24;
    } 

    p = strchr(base, ':');
    if (p == NULL) {
        return -1;
    }

    version = atoi(p + 1);
    if (version == 0) {
        VLOG_ERROR("cvd", "__ubuntu_get_base_number: unsupported base image %s\n", base);
        return -1;
    }
    return version;
}

/**
 * @brief Build the expected Ubuntu base archive name for a selector.
 * @param base Base selector such as "ubuntu:24".
 * @return Allocated archive name string, or NULL on failure.
 */
static char* __ubuntu_get_base_image_name(const char* base) {
    char tmp[1024];
    int  version = __ubuntu_get_base_number(base);

    snprintf(&tmp[0], sizeof(tmp), 
        "ubuntu-base-%i.04-base-%s.tar.gz",
        version,
        CHEF_ARCHITECTURE_STR
    );
    return platform_strdup(&tmp[0]);
}

/**
 * @brief Build the upstream Ubuntu base archive URL for a selector.
 * @param base Base selector such as "ubuntu:24".
 * @return Allocated URL string, or NULL on failure.
 */
static char* __ubuntu_get_base_image_url(const char* base) {
    char tmp[1024];
    int  version = __ubuntu_get_base_number(base);

    snprintf(&tmp[0], sizeof(tmp), 
        "https://chef-store-eu-basic.s3.de.io.cloud.ovh.net/build-bases/ubuntu-base-%i.04-base-%s.tar.gz",
        version,
        CHEF_ARCHITECTURE_STR
    );
    return platform_strdup(&tmp[0]);
}

/**
 * @brief Resolve a cached Ubuntu base archive and unpack it into the requested rootfs path.
 * @param path Destination directory for the unpacked rootfs.
 * @param base Base image string, e.g., "ubuntu:24".
 * @return 0 on success, non-zero on failure.
 */
extern int containerv_disk_setup_ubuntu_rootfs(const char* path, const char* base);

#endif // !__CONTAINERV_DISK_UBUNTU_H__
