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

#include <errno.h>
#include <chef/platform.h>
#include <stdlib.h>
#include <string.h>

// include dirent.h for directory operations
#if defined(_WIN32) || defined(_WIN64)
#include <dirent_win32.h>
#else
#include <dirent.h>
#endif

static int __add_file(const struct dirent* dp, const char* path, const char* subPath, struct list* files)
{
    struct platform_file_entry* entry;

    entry = (struct platform_file_entry*)calloc(1, sizeof(struct platform_file_entry));
    if (entry == NULL) {
        return -1;
    }

    entry->name     = platform_strdup(dp->d_name);
    entry->path     = platform_strdup(path);
    entry->sub_path = subPath != NULL ? platform_strdup(subPath) : NULL;
    if (entry->name == NULL || entry->path == NULL) {
        free(entry->name);
        free(entry->path);
        free(entry->sub_path);
        free(entry);
        return -1;
    }

    switch (dp->d_type) {
        case DT_REG:
            entry->type = PLATFORM_FILETYPE_FILE;
            break;
        case DT_DIR: {
            entry->type = PLATFORM_FILETYPE_DIRECTORY;
        } break;
        case DT_LNK:
            entry->type = PLATFORM_FILETYPE_SYMLINK;
            break;
        default:
            entry->type = PLATFORM_FILETYPE_UNKNOWN;
            break;
    }

    list_add(files, &entry->list_header);
    return 0;
}

static int __read_directory(const char* path, const char* subPath, int recursive, struct list* files)
{
    struct dirent* dp;
    DIR*           d;
    int            status = 0;

    if (!path) {
        errno = EINVAL;
        return -1;
    }

    if ((d = opendir(path)) == NULL) {
        if (errno == ENOENT) {
            return 0;
        }
        return -1;
    }

    while ((dp = readdir(d))) {
        char* combinedPath;
        char* combinedSubPath;

        if (strcmp(dp->d_name,".") == 0 || strcmp(dp->d_name,"..") == 0) {
             continue;
        }

        combinedPath    = strpathcombine(path, dp->d_name);
        combinedSubPath = strpathcombine(subPath, dp->d_name);
        if (!combinedPath || !combinedSubPath) {
            free((void*)combinedPath);
            break;
        }

        if (recursive && dp->d_type == DT_DIR) {
            status = __read_directory(combinedPath, combinedSubPath, recursive, files);
        }
        else {
            status = __add_file(dp, combinedPath, combinedSubPath, files);
        }

        free((void*)combinedPath);
        free((void*)combinedSubPath);
        if (status) {
            break;
        }
    }
    closedir(d);
    return status;
}

int platform_getfiles(const char* path, int recursive, struct list* files)
{
    if (!path || !files) {
        errno = EINVAL;
        return -1;
    }

    return __read_directory(path, NULL, recursive, files);
}

void platform_getfiles_destroy(struct list* files)
{
    struct list_item* item;

    if (files == NULL) {
        return;
    }

    for (item = files->head; item != NULL;) {
        struct platform_file_entry* entry = (struct platform_file_entry*)item;
        item = item->next;

        free(entry->name);
        free(entry->path);
        free(entry->sub_path);
        free(entry);
    }
    list_init(files);
    return 0;
}
