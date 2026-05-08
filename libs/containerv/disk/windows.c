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
#include "common.h"
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <vlog.h>

/* Derive a stable cache archive name by prefixing the source suffix with a URL hash. */
static int __build_cached_archive_name(const char* url, uint64_t hash, char* buffer, size_t buffer_size)
{
    const char* name = url;
    const char* slash;
    const char* suffix;
    int         written;

    if (url == NULL || buffer == NULL || buffer_size == 0) {
        errno = EINVAL;
        return -1;
    }

    slash = strrchr(url, '/');
    if (slash != NULL && slash[1] != '\0') {
        name = slash + 1;
    }

    suffix = strchr(name, '.');
    if (suffix == NULL) {
        suffix = ".archive";
    }

    written = snprintf(buffer, buffer_size, "%016llx%s", (unsigned long long)hash, suffix);
    if (written < 0 || (size_t)written >= buffer_size) {
        errno = ENOMEM;
        return -1;
    }
    return 0;
}

/* Reuse the remote file name as the cache entry name for WCOW archives. */
static char* __archive_name_from_url(const char* url)
{
    const char* name = url;
    const char* slash;

    if (url == NULL) {
        errno = EINVAL;
        return NULL;
    }

    slash = strrchr(url, '/');
    if (slash != NULL && slash[1] != '\0') {
        name = slash + 1;
    }
    return platform_strdup(name);
}

/* Keep a validated LCOW bundle unpacked behind a readiness marker in the shared cache. */
static int __ensure_cached_lcow_bundle(const char* archive_path, const char* bundle_path)
{
    char* marker = NULL;
    int   status = -1;

    marker = strpathcombine(bundle_path, "uvm.ready");
    if (marker == NULL) {
        errno = ENOMEM;
        return -1;
    }

    if (!containerv_disk_path_exists(marker) || containerv_disk_validate_lcow_uvm(bundle_path) != 0) {
        if (containerv_disk_extract_archive(archive_path, bundle_path, 1, 0) != 0) {
            goto cleanup;
        }

        if (containerv_disk_validate_lcow_uvm(bundle_path) != 0) {
            goto cleanup;
        }

        if (containerv_disk_write_marker(marker) != 0) {
            goto cleanup;
        }
    }

    status = 0;

cleanup:
    free(marker);
    return status;
}

int containerv_disk_setup_wcow_uvm(const char* path, const char* base)
{
    char* imageCache = NULL;
    char* imageName = NULL;
    char* imageUrl = NULL;
    char* archivePath = NULL;
    int   status;
    VLOG_DEBUG("cvd", "containerv_disk_setup_wcow_uvm(path=%s, base=%s)\n", path, base);
    
    imageCache = strpathcombine(chef_dirs_cache(), "uvm");
    if (imageCache == NULL) {
        VLOG_ERROR("cvd", "failed to allocate memory for image path\n");
        return -1;
    }

    status = platform_mkdir(imageCache);
    if (status) {
        VLOG_ERROR("cvd", "failed to create directory %s\n", imageCache);
        goto exit;
    }

    imageUrl = __resolve_windows_wcow_base_url(base);
    if (imageUrl == NULL) {
        VLOG_ERROR("cvd", "failed to allocate memory for base image url\n");
        status = -1;
        goto exit;
    }

    imageName = __archive_name_from_url(imageUrl);
    if (imageName == NULL) {
        VLOG_ERROR("cvd", "failed to allocate memory for base image name\n");
        status = -1;
        goto exit;
    }

    VLOG_TRACE("cvd", "downloading %s\n", imageUrl);
    status = containerv_disk_cache_archive(imageCache, imageName, imageUrl, &archivePath);
    if (status) {
        VLOG_ERROR("cvd", "failed to download windows image\n");
        goto exit;
    }

    VLOG_TRACE("cvd", "unpacking %s into %s\n", archivePath, path);
    status = containerv_disk_extract_archive(archivePath, path, 0, 0);
    if (status) {
        VLOG_ERROR("cvd", "failed to unpack windows image\n");
        goto exit;
    }

exit:
    free(archivePath);
    free(imageCache);
    free(imageName);
    free(imageUrl);
    return status;
}

/* Return the first optional bundle file present under the LCOW image directory. */
static char* __find_optional_bundle_file(const char* image_path, const char* const* candidates)
{
    for (int i = 0; candidates[i] != NULL; ++i) {
        char* candidate_path = strpathcombine(image_path, candidates[i]);
        int   exists = containerv_disk_path_exists(candidate_path);
        free(candidate_path);
        if (exists) {
            return platform_strdup(candidates[i]);
        }
    }
    return NULL;
}

/* Trim surrounding whitespace in place for text files embedded in the bundle. */
static void __trim_whitespace(char* text)
{
    char* start;
    char* end;

    if (text == NULL) {
        return;
    }

    start = text;
    while (*start != '\0' && isspace((unsigned char)*start)) {
        start++;
    }

    end = start + strlen(start);
    while (end > start && isspace((unsigned char)end[-1])) {
        end--;
    }
    *end = '\0';

    if (start != text) {
        memmove(text, start, (size_t)(end - start) + 1);
    }
}

/* Read and normalize the optional LCOW boot parameter file when present. */
static char* __read_boot_parameters_file(const char* image_path)
{
    char*  path;
    void*  buffer = NULL;
    size_t length = 0;
    char*  text;

    path = strpathcombine(image_path, "boot_parameters");
    if (path == NULL) {
        return NULL;
    }

    if (!containerv_disk_path_exists(path)) {
        free(path);
        return NULL;
    }

    if (platform_readfile(path, &buffer, &length) != 0) {
        free(path);
        return NULL;
    }
    free(path);

    text = calloc(length + 1, 1);
    if (text == NULL) {
        free(buffer);
        errno = ENOMEM;
        return NULL;
    }

    if (length != 0) {
        memcpy(text, buffer, length);
    }
    free(buffer);
    __trim_whitespace(text);
    if (text[0] == '\0') {
        free(text);
        return NULL;
    }
    return text;
}

static int __bundle_has_any_file(const char* image_path, const char* const* candidates)
{
    char* detected;

    detected = __find_optional_bundle_file(image_path, candidates);
    if (detected == NULL) {
        return 0;
    }

    free(detected);
    return 1;
}

int containerv_disk_validate_lcow_uvm(const char* image_path)
{
    static const char* const legacy_disk_candidates[] = { "uvm.vhdx", "uvm.vhd", NULL };
    static const char* const kernel_candidates[] = { "vmlinux", "kernel", "kernel64", NULL };
    static const char* const initrd_candidates[] = { "initrd.img", "initrd", NULL };
    static const char* const rootfs_candidates[] = { "rootfs.vhd", "rootfs.vhdx", NULL };
    int has_legacy_disk;
    int has_kernel;
    int has_initrd;
    int has_rootfs;

    if (!containerv_disk_path_is_directory(image_path)) {
        errno = ENOENT;
        return -1;
    }

    has_legacy_disk = __bundle_has_any_file(image_path, legacy_disk_candidates);
    if (has_legacy_disk) {
        return 0;
    }

    has_kernel = __bundle_has_any_file(image_path, kernel_candidates);
    has_initrd = __bundle_has_any_file(image_path, initrd_candidates);
    has_rootfs = __bundle_has_any_file(image_path, rootfs_candidates);
    if (has_kernel && (has_initrd || has_rootfs)) {
        return 0;
    }

    errno = ENOENT;
    return -1;
}

int containerv_disk_lcow_detect_uvm_files(
    const char* image_path,
    char**      kernel_file_out,
    char**      initrd_file_out,
    char**      boot_parameters_out)
{
    static const char* const kernel_candidates[] = { "vmlinux", "kernel", "kernel64", NULL };
    static const char* const initrd_candidates[] = { "initrd.img", "initrd", NULL };

    if (kernel_file_out != NULL) {
        *kernel_file_out = NULL;
    }
    if (initrd_file_out != NULL) {
        *initrd_file_out = NULL;
    }
    if (boot_parameters_out != NULL) {
        *boot_parameters_out = NULL;
    }

    if (containerv_disk_validate_lcow_uvm(image_path) != 0) {
        return -1;
    }

    if (kernel_file_out != NULL) {
        *kernel_file_out = __find_optional_bundle_file(image_path, kernel_candidates);
    }
    if (initrd_file_out != NULL) {
        *initrd_file_out = __find_optional_bundle_file(image_path, initrd_candidates);
    }
    if (boot_parameters_out != NULL) {
        *boot_parameters_out = __read_boot_parameters_file(image_path);
    }
    return 0;
}

int containerv_disk_setup_lcow_uvm(
    const struct containerv_disk_lcow_uvm_config* config,
    char**                                        uvmImageOut)
{
    const char* configuredUrl;
    char*    uvmUrl = NULL;
    char*    uvmDirectory = NULL;
    uint64_t uvmUrlHash;
    char     uvmCacheKey[32];
    char     archiveName[64];
    char*    archivePath = NULL;
    char*    uvmUnpackDirectory = NULL;
    int      status = -1;

    if (uvmImageOut == NULL) {
        errno = EINVAL;
        return -1;
    }
    *uvmImageOut = NULL;
    
    // TODO: We need to implement a versioning system to detect when
    // we should update the lcow UVM assets.
    configuredUrl = (config != NULL && config->uvm_url != NULL && config->uvm_url[0] != '\0') ? config->uvm_url : NULL;
    uvmUrl = configuredUrl != NULL ? platform_strdup(configuredUrl) : __resolve_windows_lcow_base_url();
    if (uvmUrl == NULL) {
        errno = ENOMEM;
        return -1;
    }

    uvmDirectory = strpathcombine(chef_dirs_cache(), "uvm");
    if (uvmDirectory == NULL) {
        errno = ENOMEM;
        goto cleanup;
    }

    if (platform_mkdir(uvmDirectory) != 0) {
        goto cleanup;
    }

    uvmUrlHash = containerv_disk_fnv1a64(uvmUrl);
    snprintf(uvmCacheKey, sizeof(uvmCacheKey), "%016llx", (unsigned long long)uvmUrlHash);
    if (__build_cached_archive_name(uvmUrl, uvmUrlHash, archiveName, sizeof(archiveName)) != 0) {
        goto cleanup;
    }

    VLOG_DEBUG("containerv[lcow]", "resolving LCOW UVM assets from %s\n", uvmUrl);
    if (containerv_disk_cache_archive(uvmDirectory, archiveName, uvmUrl, &archivePath) != 0) {
        VLOG_ERROR("containerv[lcow]", "failed to cache LCOW UVM assets from %s\n", uvmUrl);
        goto cleanup;
    }

    uvmUnpackDirectory = strpathcombine(uvmDirectory, uvmCacheKey);
    if (uvmUnpackDirectory == NULL) {
        errno = ENOMEM;
        goto cleanup;
    }

    if (__ensure_cached_lcow_bundle(archivePath, uvmUnpackDirectory) != 0) {
        VLOG_ERROR("containerv[lcow]", "failed to prepare cached LCOW UVM bundle at %s\n", uvmUnpackDirectory);
        goto cleanup;
    }

    *uvmImageOut = uvmUnpackDirectory;
    uvmUnpackDirectory = NULL;
    status = 0;

cleanup:
    free(archivePath);
    free(uvmUrl);
    free(uvmDirectory);
    free(uvmUnpackDirectory);
    return status;
}
