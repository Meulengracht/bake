/**
 * Copyright 2023, Philip Meulengracht
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

#include <chef/utils_vafs.h>
#include <errno.h>
#include <libingredient.h>
#include <vafs/vafs.h>
#include <vafs/directory.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zstd.h>

struct VaFsFeatureFilter {
    struct VaFsFeatureHeader Header;
};

static struct VaFsGuid g_headerGuid    = CHEF_PACKAGE_HEADER_GUID;
static struct VaFsGuid g_overviewGuid  = VA_FS_FEATURE_OVERVIEW;
static struct VaFsGuid g_filterGuid    = VA_FS_FEATURE_FILTER;
static struct VaFsGuid g_filterOpsGuid = VA_FS_FEATURE_FILTER_OPS;

static int __zstd_decode(void* Input, uint32_t InputLength, void* Output, uint32_t* OutputLength)
{
    /* Read the content size from the frame header. For simplicity we require
     * that it is always present. By default, zstd will write the content size
     * in the header when it is known. If you can't guarantee that the frame
     * content size is always written into the header, either use streaming
     * decompression, or ZSTD_decompressBound().
     */
    size_t             decompressedSize;
    unsigned long long contentSize = ZSTD_getFrameContentSize(Input, InputLength);
    if (contentSize == ZSTD_CONTENTSIZE_ERROR || contentSize == ZSTD_CONTENTSIZE_UNKNOWN) {
        fprintf(stderr, "__zstd_decode: failed to get frame content size\n");
        return -1;
    }
    
    /* Decompress.
     * If you are doing many decompressions, you may want to reuse the context
     * and use ZSTD_decompressDCtx(). If you want to set advanced parameters,
     * use ZSTD_DCtx_setParameter().
     */
    decompressedSize = ZSTD_decompress(Output, *OutputLength, Input, InputLength);
    if (ZSTD_isError(decompressedSize)) {
        return -1;
    }
    *OutputLength = (uint32_t)decompressedSize;
    return 0;
}

static int __set_filter_ops(
    struct VaFs* vafs)
{
    struct VaFsFeatureFilterOps filterOps;

    memcpy(&filterOps.Header.Guid, &g_filterOpsGuid, sizeof(struct VaFsGuid));

    filterOps.Header.Length = sizeof(struct VaFsFeatureFilterOps);
    filterOps.Encode = NULL;
    filterOps.Decode = __zstd_decode;

    return vafs_feature_add(vafs, &filterOps.Header);
}

static int __handle_filter(struct VaFs* vafs)
{
    struct VaFsFeatureFilter* filter;
    int                       status;

    status = vafs_feature_query(vafs, &g_filterGuid, (struct VaFsFeatureHeader**)&filter);
    if (status) {
        // no filter present
        return 0;
    }
    return __set_filter_ops(vafs);
}

static int __handle_overview(struct VaFs* vafsHandle, struct ingredient* ingredient)
{
    struct VaFsFeatureOverview* overview;
    int                         status;

    status = vafs_feature_query(vafsHandle, &g_overviewGuid, (struct VaFsFeatureHeader**)&overview);
    if (status) {
        fprintf(stderr, "unmkvafs: failed to query feature overview - %i\n", errno);
        return -1;
    }

    ingredient->file_count      = overview->Counts.Files;
    ingredient->directory_count = overview->Counts.Directories;
    ingredient->symlink_count   = overview->Counts.Symlinks;
    return 0;
}

static struct ingredient* __ingredient_new(void)
{
    struct ingredient* ingredient;

    ingredient = calloc(1, sizeof(struct ingredient));
    if (ingredient == NULL) {
        return NULL;
    }
    return ingredient;
}

static void __ingredient_delete(struct ingredient* ingredient)
{
    if (ingredient == NULL) {
        return;
    }

    chef_package_free(ingredient->package);
    chef_version_free(ingredient->version);
    vafs_directory_close(ingredient->root_handle);
    vafs_close(ingredient->vafs);
    free(ingredient);
}

int ingredient_open(const char* path, struct ingredient** ingredientOut)
{
    struct VaFs*                vafsHandle;
    struct VaFsDirectoryHandle* directoryHandle;
    struct ingredient*          ingredient;
    enum chef_package_type      packType;
    int                         status;

    ingredient = __ingredient_new();
    if (ingredient == NULL) {
        return -1;
    }

    status = vafs_open_file(path, &vafsHandle);
    if (status) {
        fprintf(stderr, "ingredient_open: cannot open vafs image: %s\n", path);
        free(ingredient);
        return status;
    }

    status = chef_package_load_vafs(vafsHandle, &ingredient->package, &ingredient->version, NULL, NULL);
    if (status) {
        fprintf(stderr, "ingredient_open: cannot open vafs image: %s\n", path);
        free(ingredient);
        return status;
    }

    status = __handle_overview(vafsHandle, ingredient);
    if (status) {
        fprintf(stderr, "ingredient_open: failed to handle image overview\n");
        __ingredient_delete(ingredient);
        return status;
    }

    status = __handle_filter(vafsHandle);
    if (status) {
        fprintf(stderr, "ingredient_open: failed to handle image filter\n");
        __ingredient_delete(ingredient);
        return status;
    }

    status = vafs_directory_open(vafsHandle, "/", &directoryHandle);
    if (status) {
        fprintf(stderr, "ingredient_open: cannot open root directory: /\n");
        __ingredient_delete(ingredient);
        return status;
    }

    *ingredientOut = ingredient;
    return 0;
}

void ingredient_close(struct ingredient* ingredient)
{
    __ingredient_delete(ingredient);
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
        fprintf(stderr, "__extract_file: unable to open file %s\n", path);
        return -1;
    }

    fileSize = vafs_file_length(fileHandle);
    if (fileSize) {
        fileBuffer = malloc(fileSize);
        if (fileBuffer == NULL) {
            fprintf(stderr, "__extract_file: unable to allocate memory for file %s\n", path);
            return -1;
        }

        vafs_file_read(fileHandle, fileBuffer, fileSize);
        fwrite(fileBuffer, 1, fileSize, file);
        free(fileBuffer);
    }
    fclose(file);
    return platform_chmod(path, vafs_file_permissions(fileHandle));
}

static int __extract_directory(
    struct VaFsDirectoryHandle* directoryHandle,
    const char*                 root,
    const char*                 path,
    ingredient_progress_cb      progressCB,
    void*                       context)
{
    struct VaFsEntry dp;
    int              status;
    char*            filepathBuffer;

    // ensure the directory exists
    if (strlen(path)) {
        if (platform_mkdir(path)) {
            fprintf(stderr, "__extract_directory: unable to create directory %s\n", path);
            return -1;
        }
    }

    do {
        status = vafs_directory_read(directoryHandle, &dp);
        if (status) {
            if (errno != ENOENT) {
                fprintf(stderr, "__extract_directory: failed to read directory '%s' - %i\n",
                    __get_relative_path(root, path), status);
                return -1;
            }
            break;
        }

        filepathBuffer = strpathcombine(path, dp.Name);
        if (filepathBuffer == NULL) {
            fprintf(stderr, "__extract_directory: unable to allocate memory for filepath\n");
            return -1;
        }

        if (progressCB != NULL) {
            progressCB(dp.Name, INGREDIENT_PROGRESS_START, context);
        }
        if (dp.Type == VaFsEntryType_Directory) {
            struct VaFsDirectoryHandle* subdirectoryHandle;
            status = vafs_directory_open_directory(directoryHandle, dp.Name, &subdirectoryHandle);
            if (status) {
                fprintf(stderr, "__extract_directory: failed to open directory '%s'\n", __get_relative_path(root, filepathBuffer));
                return -1;
            }

            status = __extract_directory(subdirectoryHandle, root, filepathBuffer, progressCB, context);
            if (status) {
                fprintf(stderr, "__extract_directory: unable to extract directory '%s'\n", __get_relative_path(root, path));
                return -1;
            }

            status = vafs_directory_close(subdirectoryHandle);
            if (status) {
                fprintf(stderr, "__extract_directory: failed to close directory '%s'\n", __get_relative_path(root, filepathBuffer));
                return -1;
            }
            if (progressCB != NULL) {
                progressCB(dp.Name, INGREDIENT_PROGRESS_DIRECTORY, context);
            }
        } else if (dp.Type == VaFsEntryType_File) {
            struct VaFsFileHandle* fileHandle;
            status = vafs_directory_open_file(directoryHandle, dp.Name, &fileHandle);
            if (status) {
                fprintf(stderr, "__extract_directory: failed to open file '%s' - %i\n",
                    __get_relative_path(root, filepathBuffer), status);
                return -1;
            }

            status = __extract_file(fileHandle, filepathBuffer);
            if (status) {
                fprintf(stderr, "__extract_directory: unable to extract file '%s'\n", __get_relative_path(root, path));
                return -1;
            }

            status = vafs_file_close(fileHandle);
            if (status) {
                fprintf(stderr, "__extract_directory: failed to close file '%s'\n", __get_relative_path(root, filepathBuffer));
                return -1;
            }
            if (progressCB != NULL) {
                progressCB(dp.Name, INGREDIENT_PROGRESS_FILE, context);
            }
        } else if (dp.Type == VaFsEntryType_Symlink) {
            const char* symlinkTarget;
            
            status = vafs_directory_read_symlink(directoryHandle, dp.Name, &symlinkTarget);
            if (status) {
                fprintf(stderr, "__extract_directory: failed to read symlink '%s' - %i\n",
                    __get_relative_path(root, filepathBuffer), status);
                return -1;
            }

            status = platform_symlink(filepathBuffer, symlinkTarget, 0 /* TODO */);
            if (status) {
                fprintf(stderr, "__extract_directory: failed to create symlink '%s' - %i\n",
                    __get_relative_path(root, filepathBuffer), status);
                return -1;
            }
            if (progressCB != NULL) {
                progressCB(dp.Name, INGREDIENT_PROGRESS_SYMLINK, context);
            }
        } else {
            fprintf(stderr, "__extract_directory: unable to extract unknown type '%s'\n", __get_relative_path(root, filepathBuffer));
            return -1;
        }
        free(filepathBuffer);
    } while(1);

    return 0;
}

int ingredient_unpack(struct ingredient* ingredient, const char* path, ingredient_progress_cb progressCB, void* context)
{
    if (ingredient == NULL || path == NULL) {
        errno = EINVAL;
        return -1;
    }
    return __extract_directory(ingredient->root_handle, path, path, progressCB, context);
}
