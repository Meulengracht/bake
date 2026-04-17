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
 * LCOW UVM asset retrieval and caching (Windows host).
 */

#include <chef/containerv/disk/lcow.h>
#include <chef/dirs.h>
#include <chef/platform.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vlog.h>

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

static int __ensure_dir(const char* path)
{
    if (path == NULL || path[0] == '\0') {
        return -1;
    }
    return platform_mkdir(path);
}

static char* __path_join3(const char* path1, const char* path2, const char* path3)
{
    char* combined = strpathcombine(path1, path2);
    char* result;

    if (combined == NULL) {
        return NULL;
    }

    result = strpathcombine(combined, path3);
    free(combined);
    return result;
}

static int __ensure_parent_dir(const char* path)
{
    char* parent;
    char* separator;

    if (path == NULL || path[0] == '\0') {
        errno = EINVAL;
        return -1;
    }

    parent = platform_strdup(path);
    if (parent == NULL) {
        errno = ENOMEM;
        return -1;
    }

    separator = strrchr(parent, '\\');
    if (separator == NULL) {
        separator = strrchr(parent, '/');
    }

    if (separator != NULL) {
        *separator = '\0';
        if (parent[0] != '\0' && __ensure_dir(parent) != 0) {
            free(parent);
            return -1;
        }
    }

    free(parent);
    return 0;
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
        errno = ENOMEM;
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

static int __copy_directory_recursive(const char* source_dir, const char* target_dir)
{
    struct list files;
    struct list_item* item;

    if (!__path_is_directory(source_dir)) {
        errno = ENOENT;
        return -1;
    }

    if (__ensure_dir(target_dir) != 0) {
        return -1;
    }

    list_init(&files);
    if (platform_getfiles(source_dir, 1, &files) != 0) {
        return -1;
    }

    list_foreach(&files, item) {
        struct platform_file_entry* entry = (struct platform_file_entry*)item;
        char*                       destination;

        if (entry->type != PLATFORM_FILETYPE_FILE) {
            continue;
        }

        destination = strpathcombine(target_dir, entry->sub_path != NULL ? entry->sub_path : entry->name);
        if (destination == NULL) {
            platform_getfiles_destroy(&files);
            errno = ENOMEM;
            return -1;
        }

        if (__ensure_parent_dir(destination) != 0 || platform_copyfile(entry->path, destination) != 0) {
            free(destination);
            platform_getfiles_destroy(&files);
            return -1;
        }
        free(destination);
    }

    platform_getfiles_destroy(&files);
    return 0;
}

static int __append_fragment(char* buffer, size_t buffer_size, size_t* index, const char* fragment)
{
    int written;

    if (*index >= buffer_size) {
        return -1;
    }

    written = snprintf(buffer + *index, buffer_size - *index, "%s", fragment);
    if (written < 0 || (size_t)written >= buffer_size - *index) {
        return -1;
    }

    *index += (size_t)written;
    return 0;
}

static int __append_token(char* buffer, size_t buffer_size, size_t* index, const char* token)
{
    if (*index != 0 && __append_fragment(buffer, buffer_size, index, " ") != 0) {
        return -1;
    }

    if (__append_fragment(buffer, buffer_size, index, "\"") != 0) {
        return -1;
    }

    for (const char* p = token; *p != '\0'; ++p) {
        char piece[3] = { 0 };

        if (*p == '\"') {
            piece[0] = '\\';
            piece[1] = '\"';
        } else {
            piece[0] = *p;
        }

        if (__append_fragment(buffer, buffer_size, index, piece) != 0) {
            return -1;
        }
    }

    return __append_fragment(buffer, buffer_size, index, "\"");
}

static int __download_and_extract_zip(const char* url, const char* dest_dir, const char* zip_path)
{
    char   arguments[8192] = { 0 };
    size_t index = 0;
    int    status;

    (void)platform_rmdir(dest_dir);
    if (__ensure_dir(dest_dir) != 0) {
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

int containerv_disk_lcow_validate_uvm(const char* image_path)
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

    if (containerv_disk_lcow_validate_uvm(image_path) != 0) {
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

int containerv_disk_lcow_import_uvm(const char* source_dir, char** image_path_out)
{
    const char* cache_root;
    char*       absolute_source;
    char*       lcow_dir;
    char*       uvm_dir;
    char*       import_dir;
    char*       target_dir;
    uint64_t    hash;
    char        key[32];

    if (image_path_out == NULL) {
        errno = EINVAL;
        return -1;
    }
    *image_path_out = NULL;

    absolute_source = platform_abspath(source_dir);
    if (absolute_source == NULL) {
        errno = ENOENT;
        return -1;
    }

    if (containerv_disk_lcow_validate_uvm(absolute_source) != 0) {
        free(absolute_source);
        return -1;
    }

    cache_root = chef_dirs_cache();
    if (cache_root == NULL) {
        free(absolute_source);
        errno = ENOENT;
        return -1;
    }

    lcow_dir = strpathcombine(cache_root, "lcow");
    uvm_dir = lcow_dir ? strpathcombine(lcow_dir, "uvm") : NULL;
    import_dir = uvm_dir ? strpathcombine(uvm_dir, "imported") : NULL;
    if (lcow_dir == NULL || uvm_dir == NULL || import_dir == NULL) {
        free(absolute_source);
        free(lcow_dir);
        free(uvm_dir);
        free(import_dir);
        errno = ENOMEM;
        return -1;
    }

    if (__ensure_dir(lcow_dir) != 0 || __ensure_dir(uvm_dir) != 0 || __ensure_dir(import_dir) != 0) {
        free(absolute_source);
        free(lcow_dir);
        free(uvm_dir);
        free(import_dir);
        return -1;
    }

    hash = __fnv1a64(absolute_source);
    snprintf(key, sizeof(key), "%016llx", (unsigned long long)hash);

    target_dir = strpathcombine(import_dir, key);
    if (target_dir == NULL) {
        free(absolute_source);
        free(lcow_dir);
        free(uvm_dir);
        free(import_dir);
        errno = ENOMEM;
        return -1;
    }

    (void)platform_rmdir(target_dir);
    if (__copy_directory_recursive(absolute_source, target_dir) != 0 || containerv_disk_lcow_validate_uvm(target_dir) != 0) {
        free(absolute_source);
        free(lcow_dir);
        free(uvm_dir);
        free(import_dir);
        free(target_dir);
        return -1;
    }

    free(absolute_source);
    free(lcow_dir);
    free(uvm_dir);
    free(import_dir);
    *image_path_out = target_dir;
    return 0;
}

int containerv_disk_lcow_resolve_uvm(
    const struct containerv_disk_lcow_uvm_config* config,
    char**                                       image_path_out)
{
    if (image_path_out == NULL) {
        errno = EINVAL;
        return -1;
    }
    *image_path_out = NULL;

    if (config == NULL || config->uvm_url == NULL || config->uvm_url[0] == '\0') {
        errno = EINVAL;
        return -1;
    }

    const char* cache_root = chef_dirs_cache();
    if (cache_root == NULL) {
        errno = ENOENT;
        return -1;
    }

    char* lcow_dir = strpathcombine(cache_root, "lcow");
    char* uvm_dir = lcow_dir ? strpathcombine(lcow_dir, "uvm") : NULL;
    if (lcow_dir == NULL || uvm_dir == NULL) {
        free(lcow_dir);
        free(uvm_dir);
        errno = ENOMEM;
        return -1;
    }

    if (__ensure_dir(lcow_dir) != 0 || __ensure_dir(uvm_dir) != 0) {
        free(lcow_dir);
        free(uvm_dir);
        return -1;
    }

    uint64_t h = __fnv1a64(config->uvm_url);
    char key[32];
    snprintf(key, sizeof(key), "%016llx", (unsigned long long)h);

    char* target_dir = strpathcombine(uvm_dir, key);
    if (target_dir == NULL) {
        free(lcow_dir);
        free(uvm_dir);
        errno = ENOMEM;
        return -1;
    }

    char* marker = strpathcombine(target_dir, "uvm.ready");
    char* zip_path = strpathcombine(uvm_dir, "uvm.zip");
    if (marker == NULL || zip_path == NULL) {
        free(lcow_dir);
        free(uvm_dir);
        free(target_dir);
        free(marker);
        free(zip_path);
        errno = ENOMEM;
        return -1;
    }

    if (!__path_exists(marker)) {
        VLOG_DEBUG("containerv[lcow]", "downloading LCOW UVM assets from %s\n", config->uvm_url);
        if (__download_and_extract_zip(config->uvm_url, target_dir, zip_path) != 0) {
            VLOG_ERROR("containerv[lcow]", "failed to download/extract LCOW UVM assets\n");
            free(lcow_dir);
            free(uvm_dir);
            free(target_dir);
            free(marker);
            free(zip_path);
            return -1;
        }
        if (containerv_disk_lcow_validate_uvm(target_dir) != 0) {
            VLOG_ERROR("containerv[lcow]", "downloaded LCOW UVM bundle is invalid: %s\n", target_dir);
            free(lcow_dir);
            free(uvm_dir);
            free(target_dir);
            free(marker);
            free(zip_path);
            return -1;
        }
        (void)__write_marker(marker);
    } else if (containerv_disk_lcow_validate_uvm(target_dir) != 0) {
        VLOG_ERROR("containerv[lcow]", "cached LCOW UVM bundle is invalid: %s\n", target_dir);
        free(lcow_dir);
        free(uvm_dir);
        free(target_dir);
        free(marker);
        free(zip_path);
        return -1;
    }

    *image_path_out = target_dir;
    free(lcow_dir);
    free(uvm_dir);
    free(marker);
    free(zip_path);
    return 0;
}
