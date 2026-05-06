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
    char* url = __resolve_windows_wcow_base_url(base);
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

static int __download_and_extract_zip(const char* url, const char* dest_dir, const char* zip_path)
{
    char   arguments[8192] = { 0 };
    size_t index = 0;
    int    status;

    (void)platform_rmdir(dest_dir);
    if (platform_mkdir(dest_dir) != 0) {
        return -1;
    }

    (void)platform_unlink(zip_path);
    status = __append_token(arguments, sizeof(arguments), &index, "-L");
    status |= __append_token(arguments, sizeof(arguments), &index, "--fail");
    status |= __append_token(arguments, sizeof(arguments), &index, "--output");
    status |= __append_token(arguments, sizeof(arguments), &index, zip_path);
    status |= __append_token(arguments, sizeof(arguments), &index, url);
    if (status != 0) {
        return -1;
    }

    status = platform_spawn("curl", arguments, NULL, &(struct platform_spawn_options) {0});
    if (status != 0) {
        return -1;
    }

    memset(arguments, 0, sizeof(arguments));
    index = 0;
    status = __append_token(arguments, sizeof(arguments), &index, "-xf");
    status |= __append_token(arguments, sizeof(arguments), &index, zip_path);
    status |= __append_token(arguments, sizeof(arguments), &index, "-C");
    status |= __append_token(arguments, sizeof(arguments), &index, dest_dir);
    if (status != 0) {
        return -1;
    }

    status = platform_spawn("tar", arguments, NULL, &(struct platform_spawn_options) {0});
    (void)platform_unlink(zip_path);
    return status;
}

int containerv_disk_setup_wcow_uvm(const char* path, const char* base)
{
    char* imageCache = NULL;
    char* imageName = NULL;
    char  tmp[PATH_MAX];
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

static uint64_t __fnv1a64(const char* s)
{
    uint64_t h = 1469598103934665603ULL;
    if (s == NULL) {
        return h;
    }
    for (const unsigned char* p = (const unsigned char*)s; *p; ++p) {
        h ^= (uint64_t)(*p);
        h *= 1099511628211ULL;
    }
    return h;
}

static int __path_exists(const char* path)
{
    struct platform_stat st;
    return (path && platform_stat(path, &st) == 0) ? 1 : 0;
}

static int __path_is_directory(const char* path)
{
    struct platform_stat st;
    return (path && platform_stat(path, &st) == 0 && st.type == PLATFORM_FILETYPE_DIRECTORY) ? 1 : 0;
}

static char* __find_optional_bundle_file(const char* image_path, const char* const* candidates)
{
    for (int i = 0; candidates[i] != NULL; ++i) {
        char* candidate_path = strpathcombine(image_path, candidates[i]);
        int   exists = __path_exists(candidate_path);
        free(candidate_path);
        if (exists) {
            return platform_strdup(candidates[i]);
        }
    }
    return NULL;
}

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

    if (!__path_exists(path)) {
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

static int __write_marker(const char* marker)
{
    FILE* f = fopen(marker, "wb");
    if (f == NULL) {
        return -1;
    }
    fputs("ok", f);
    fclose(f);
    return 0;
}

int containerv_disk_validate_lcow_uvm(const char* image_path)
{
    char* uvm_vhdx;
    int   status;

    if (!__path_is_directory(image_path)) {
        errno = ENOENT;
        return -1;
    }

    uvm_vhdx = strpathcombine(image_path, "uvm.vhdx");
    if (uvm_vhdx == NULL) {
        errno = ENOMEM;
        return -1;
    }

    status = __path_exists(uvm_vhdx) ? 0 : -1;
    free(uvm_vhdx);
    if (status != 0) {
        errno = ENOENT;
        return -1;
    }
    return 0;
}

int containerv_disk_lcow_detect_uvm_files(
    const char* image_path,
    char**      kernel_file_out,
    char**      initrd_file_out,
    char**      boot_parameters_out)
{
    static const char* const kernel_candidates[] = { "kernel", "kernel64", NULL };
    static const char* const initrd_candidates[] = { "initrd", "initrd.img", NULL };

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

int containerv_disk_setup_lcow_uvm(char** uvmImageOut)
{
    char*    uvmUrl;
    char*    uvmDirectory = NULL;
    uint64_t uvmUrlHash;
    char     uvmCacheKey[32];
    char*    uvmUnpackDirectory = NULL;
    int      status;

    if (uvmImageOut == NULL) {
        return -1;
    }
    
    // TODO: We need to implement a versioning system to detect when
    // we should update the lcow UVM assets.
    uvmUrl = __resolve_windows_lcow_base_url();

    uvmDirectory = strpathcombine(chef_dirs_cache(), "uvm");
    if (uvmDirectory == NULL) {
        return -1;
    }

    status = platform_mkdir(uvmDirectory);
    if (status) {
        goto cleanup;
    }

    uvmUrlHash = __fnv1a64(uvmUrl);
    snprintf(uvmCacheKey, sizeof(uvmCacheKey), "%016llx", (unsigned long long)uvmUrlHash);

    uvmUnpackDirectory = strpathcombine(uvmDirectory, uvmCacheKey);
    if (uvmUnpackDirectory == NULL) {
        free(uvmDirectory);
        return -1;
    }

    char* marker = strpathcombine(uvmUnpackDirectory, "uvm.ready");
    char* zip_path = strpathcombine(uvmDirectory, "uvm.zip");
    if (marker == NULL || zip_path == NULL) {
        status = -1;
        goto cleanup;
    }

    if (!__path_exists(marker)) {
        VLOG_DEBUG("containerv[lcow]", "downloading LCOW UVM assets from %s\n", uvmUrl);
        if (__download_and_extract_zip(uvmUrl, uvmUnpackDirectory, zip_path) != 0) {
            VLOG_ERROR("containerv[lcow]", "failed to download/extract LCOW UVM assets\n");
            status = -1;
            goto cleanup;
        }
        if (containerv_disk_validate_lcow_uvm(uvmUnpackDirectory) != 0) {
            VLOG_ERROR("containerv[lcow]", "downloaded LCOW UVM bundle is invalid: %s\n", uvmUnpackDirectory);
            status = -1;
            goto cleanup;
        }
        (void)__write_marker(marker);
    } else if (containerv_disk_validate_lcow_uvm(uvmUnpackDirectory) != 0) {
        VLOG_ERROR("containerv[lcow]", "cached LCOW UVM bundle is invalid: %s\n", uvmUnpackDirectory);
        status = -1;
        goto cleanup;
    }

    *uvmImageOut = uvmUnpackDirectory;

cleanup:
    free(uvmDirectory);
    free(marker);
    free(zip_path);
    return 0;
}
