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

#include "common.h"

#include <chef/platform.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    if (token == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (*index != 0 && __append_fragment(buffer, buffer_size, index, " ") != 0) {
        return -1;
    }

    if (__append_fragment(buffer, buffer_size, index, "\"") != 0) {
        return -1;
    }

    for (const char* p = token; *p != '\0'; ++p) {
        char piece[3] = { 0 };

        if (*p == '"') {
            piece[0] = '\\';
            piece[1] = '"';
        } else {
            piece[0] = *p;
        }

        if (__append_fragment(buffer, buffer_size, index, piece) != 0) {
            return -1;
        }
    }

    return __append_fragment(buffer, buffer_size, index, "\"");
}

int containerv_disk_path_exists(const char* path)
{
    struct platform_stat st;

    return (path != NULL && platform_stat(path, &st) == 0) ? 1 : 0;
}

int containerv_disk_path_is_directory(const char* path)
{
    struct platform_stat st;

    return (path != NULL && platform_stat(path, &st) == 0 && st.type == PLATFORM_FILETYPE_DIRECTORY) ? 1 : 0;
}

uint64_t containerv_disk_fnv1a64(const char* text)
{
    uint64_t hash = 1469598103934665603ULL;

    if (text == NULL) {
        return hash;
    }

    for (const unsigned char* current = (const unsigned char*)text; *current != '\0'; ++current) {
        hash ^= (uint64_t)(*current);
        hash *= 1099511628211ULL;
    }
    return hash;
}

int containerv_disk_write_marker(const char* marker_path)
{
    FILE* marker;

    marker = fopen(marker_path, "wb");
    if (marker == NULL) {
        return -1;
    }

    fputs("ok", marker);
    return fclose(marker);
}

int containerv_disk_cache_archive(
    const char* cache_dir,
    const char* archive_name,
    const char* url,
    char**      archive_path_out)
{
    char*  archive_path = NULL;
    char   arguments[8192] = { 0 };
    size_t index = 0;
    int    status;

    if (cache_dir == NULL || archive_name == NULL || url == NULL || archive_path_out == NULL) {
        errno = EINVAL;
        return -1;
    }

    *archive_path_out = NULL;
    if (platform_mkdir(cache_dir) != 0) {
        return -1;
    }

    archive_path = strpathcombine(cache_dir, archive_name);
    if (archive_path == NULL) {
        errno = ENOMEM;
        return -1;
    }

    if (containerv_disk_path_exists(archive_path)) {
        *archive_path_out = archive_path;
        return 0;
    }

    if (__append_token(arguments, sizeof(arguments), &index, "-L") != 0 ||
        __append_token(arguments, sizeof(arguments), &index, "--fail") != 0 ||
        __append_token(arguments, sizeof(arguments), &index, "--output") != 0 ||
        __append_token(arguments, sizeof(arguments), &index, archive_path) != 0 ||
        __append_token(arguments, sizeof(arguments), &index, url) != 0) {
        free(archive_path);
        return -1;
    }

    status = platform_spawn("curl", arguments, NULL, &(struct platform_spawn_options) {0});
    if (status != 0) {
        (void)platform_unlink(archive_path);
        free(archive_path);
        return -1;
    }

    *archive_path_out = archive_path;
    return 0;
}

int containerv_disk_extract_archive(
    const char* archive_path,
    const char* destination,
    int         recreate_destination,
    int         include_xattrs)
{
    char   arguments[8192] = { 0 };
    size_t index = 0;

    if (archive_path == NULL || destination == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (recreate_destination) {
        (void)platform_rmdir(destination);
    }

    if (platform_mkdir(destination) != 0) {
        return -1;
    }

    if (__append_token(arguments, sizeof(arguments), &index, "-xf") != 0 ||
        __append_token(arguments, sizeof(arguments), &index, archive_path) != 0) {
        return -1;
    }

#if !defined(_WIN32) && !defined(_WIN64)
    if (include_xattrs && __append_token(arguments, sizeof(arguments), &index, "--xattrs-include=*") != 0) {
        return -1;
    }
#else
    (void)include_xattrs;
#endif

    if (__append_token(arguments, sizeof(arguments), &index, "-C") != 0 ||
        __append_token(arguments, sizeof(arguments), &index, destination) != 0) {
        return -1;
    }

    return platform_spawn("tar", arguments, NULL, &(struct platform_spawn_options) {0});
}