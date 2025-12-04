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

#ifndef __UBUNTU_H__
#define __UBUNTU_H__

#include <chef/platform.h>
#include <stdlib.h>
#include <string.h>
#include <vlog.h>

#define UBUNTU_24_LTS_VERSION "24.04"
#define UBUNTU_24_LTS_RELEASE "3"

#define UBUNTU_22_LTS_VERSION "22.04"
#define UBUNTU_22_LTS_RELEASE "5"


// helper to get the base image number from a string like "ubuntu:24"
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

static const char* __ubuntu_get_base_release(const char* base) {
    int version = __ubuntu_get_base_number(base);
    switch (version) {
        case 24:
            return UBUNTU_24_LTS_RELEASE;
        case 22:
            return UBUNTU_22_LTS_RELEASE;
        default:
            VLOG_ERROR("cvd", "__ubuntu_get_base_release: unsupported base image %s\n", base);
            return NULL;
    }
}

static char* __ubuntu_get_base_image_name(const char* base) {
    char        tmp[1024];
    int         version = __ubuntu_get_base_number(base);
    const char* release = __ubuntu_get_base_release(base);
    if (release == NULL) {
        return NULL;
    }

    snprintf(&tmp[0], sizeof(tmp), 
        "ubuntu-base-%i.04.%s-base-%s.tar.gz",
        version,
        release,
        CHEF_ARCHITECTURE_STR
    );
    return platform_strdup(&tmp[0]);
}

static char* __ubuntu_get_base_image_url(const char* base) {
    char        tmp[1024];
    int         version = __ubuntu_get_base_number(base);
    const char* release = __ubuntu_get_base_release(base);
    if (release == NULL) {
        return NULL;
    }

    snprintf(&tmp[0], sizeof(tmp), 
        "https://cdimage.ubuntu.com/ubuntu-base/releases/%i.04/release/ubuntu-base-%i.04.%s-base-%s.tar.gz",
        version,
        version,
        release,
        CHEF_ARCHITECTURE_STR
    );
    return platform_strdup(&tmp[0]);
}

#endif // !__UBUNTU_H__
