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
 * Image helpers for Windows-based container disks.
 */

#include <chef/containerv/disk/windows.h>
#include <chef/dirs.h>
#include <chef/platform.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vlog.h>

static int __file_exists(const char* path)
{
    struct platform_stat stats;
    return platform_stat(path, &stats) == 0 ? 1 : 0;
}

static int __download_container_base(const char* base, const char* dir)
{
    char  tmp[PATH_MAX];
    int   status;
    char* url = __windows_base_archive(base);
    if (url == NULL) {
        VLOG_ERROR("cvd", "failed to allocate memory for base image url\n");
        return -1;
    }

    snprintf(&tmp[0], sizeof(tmp), "-P %s %s", dir, url);

    VLOG_TRACE("cvd", "downloading %s\n", url);
    status = platform_spawn(
        "wget", &tmp[0], NULL, &(struct platform_spawn_options) { }
    );
    if (status) {
        VLOG_ERROR("cvd", "failed to download windows container image\n");
    }
    free(url);
    return status;
}

int containerv_disk_setup_windows_image(const char* path, const char* base)
{
    char* imageCache = NULL;
    char* imageName = NULL;
    char  tmp[PATH_MAX];
    int   status;
    VLOG_DEBUG("cvd", "containerv_disk_setup_windows_image(path=%s, base=%s)\n", path, base);
    
    imageCache = strpathcombine(chef_dirs_cache(), "rootfs");
    if (imageCache == NULL) {
        VLOG_ERROR("cvd", "failed to allocate memory for image path\n");
        return -1;
    }

    status = platform_mkdir(imageCache);
    if (status) {
        VLOG_ERROR("cvd", "failed to create directory %s\n", imageCache);
        goto exit;
    }

    imageName = __windows_get_base_image_name(base);
    if (imageName == NULL) {
        VLOG_ERROR("cvd", "failed to allocate memory for base image name\n");
        status = -1;
        goto exit;
    }

    snprintf(&tmp[0], sizeof(tmp), "%s/%s", imageCache, imageName);

    if (!__file_exists(&tmp[0])) {
        status = __download_container_base(base, imageCache);
        if (status) {
            VLOG_ERROR("cvd", "failed to download windows image\n");
            goto exit;
        }
    }

    snprintf(
        &tmp[0],
        sizeof(tmp),
        "-x --xattrs-include=* -f %s/%s -C %s",
        imageCache, imageName, path
    );

    VLOG_TRACE("cvd", "unpacking %s/%s\n", imageCache, imageName);
    status = platform_spawn(
        "tar", &tmp[0], NULL, &(struct platform_spawn_options) {
        }
    );
    if (status) {
        VLOG_ERROR("cvd", "failed to unpack windows image\n");
        goto exit;
    }

    status = __fixup_dns(path);
    if (status) {
        VLOG_ERROR("cvd", "failed to fix dns settings\n");
        goto exit;
    }

exit:
    free(imageCache);
    free(imageName);
    return status;
}
