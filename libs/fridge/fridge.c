/**
 * Copyright 2022, Philip Meulengracht
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
#include <libfridge.h>


static const char* __get_relative_path(
    const char* root,
    const char* path)
{
    const char* relative = path;
    if (strncmp(path, root, strlen(root)) == 0)
        relative = path + strlen(root);
    return relative;
}

static int __extract_file(
    struct VaFsFileHandle* fileHandle,
    const char*            path)
{
    FILE* file;
    long  fileSize;
    void* fileBuffer;
    int   status;

    if ((file = fopen(path, "wb+")) == NULL) {
        fprintf(stderr, "unmkvafs: unable to open file %s\n", path);
        return -1;
    }

    fileSize = vafs_file_length(fileHandle);
    fileBuffer = malloc(fileSize);
    if (fileBuffer == NULL) {
        fprintf(stderr, "unmkvafs: unable to allocate memory for file %s\n", path);
        return -1;
    }

    vafs_file_read(fileHandle, fileBuffer, fileSize);
    fwrite(fileBuffer, 1, fileSize, file);
    
    free(fileBuffer);
    fclose(file);
    return 0;
}

static int __extract_directory(
    struct VaFsDirectoryHandle* directoryHandle,
    const char*                 root,
    const char*                 path)
{
    struct VaFsEntry dp;
    int              status;
    char*            filepathBuffer;

    // ensure the directory exists
    if (strlen(path)) {
        if (plotform_mkdir(path)) {
            fprintf(stderr, "unmkvafs: unable to create directory %s\n", path);
            return -1;
        }
    }

    do {
        status = vafs_directory_read(directoryHandle, &dp);
        if (status) {
            if (errno != ENOENT) {
                fprintf(stderr, "unmkvafs: failed to read directory '%s' - %i\n",
                    __get_relative_path(root, path), status);
                return -1;
            }
            break;
        }

        filepathBuffer = malloc(strlen(path) + strlen(dp.Name) + 2);
        sprintf(filepathBuffer, "%s/%s", path, dp.Name);

        if (dp.Type == VaFsEntryType_Directory) {
            struct VaFsDirectoryHandle* subdirectoryHandle;
            status = vafs_directory_open_directory(directoryHandle, dp.Name, &subdirectoryHandle);
            if (status) {
                fprintf(stderr, "unmkvafs: failed to open directory '%s'\n", __get_relative_path(root, filepathBuffer));
                return -1;
            }

            status = __extract_directory(subdirectoryHandle, root, filepathBuffer);
            if (status) {
                fprintf(stderr, "unmkvafs: unable to extract directory '%s'\n", __get_relative_path(root, path));
                return -1;
            }

            status = vafs_directory_close(subdirectoryHandle);
            if (status) {
                fprintf(stderr, "unmkvafs: failed to close directory '%s'\n", __get_relative_path(root, filepathBuffer));
                return -1;
            }
        }
        else {
            struct VaFsFileHandle* fileHandle;
            status = vafs_directory_open_file(directoryHandle, dp.Name, &fileHandle);
            if (status) {
                fprintf(stderr, "unmkvafs: failed to open file '%s' - %i\n",
                    __get_relative_path(root, filepathBuffer), status);
                return -1;
            }

            status = __extract_file(fileHandle, filepathBuffer);
            if (status) {
                fprintf(stderr, "unmkvafs: unable to extract file '%s'\n", __get_relative_path(root, path));
                return -1;
            }

            status = vafs_file_close(fileHandle);
            if (status) {
                fprintf(stderr, "unmkvafs: failed to close file '%s'\n", __get_relative_path(root, filepathBuffer));
                return -1;
            }
        }
        free(filepathBuffer);
    } while(1);

    return 0;
}

int fridge_initialize(void)
{
    return 0;
}

int fridge_cleanup(void)
{
    return 0;
}

int fridge_use_ingredient(struct fridge_ingredient *ingredient)
{
    errno = ENOTSUP;
    return -1;
}
