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

static enum chef_package_type __get_pack_type(struct VaFs* vafsHandle)
{
    struct chef_vafs_feature_package_header* packageHeader;
    int                                      status;

    status = vafs_feature_query(vafsHandle, &g_headerGuid, (struct VaFsFeatureHeader**)&packageHeader);
    if (status) {
        fprintf(stderr, "__get_unpack_path: failed to query package header\n");
        return CHEF_PACKAGE_TYPE_UNKNOWN;
    }
    return packageHeader->type;
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
        return -1;
    }

    status = __handle_overview(vafsHandle, ingredient);
    if (status) {
        fprintf(stderr, "ingredient_open: failed to handle image overview\n");
        __ingredient_delete(ingredient);
        return -1;
    }

    status = __handle_filter(vafsHandle);
    if (status) {
        fprintf(stderr, "ingredient_open: failed to handle image filter\n");
        __ingredient_delete(ingredient);
        return -1;
    }

    status = vafs_directory_open(vafsHandle, "/", &directoryHandle);
    if (status) {
        fprintf(stderr, "ingredient_open: cannot open root directory: /\n");
        __ingredient_delete(ingredient);
        return -1;
    }

    // detect the type of ingredient we are unpacking.
    ingredient->type = __get_pack_type(vafsHandle);
    
    *ingredientOut = ingredient;
    return 0;
}

void ingredient_close(struct ingredient* ingredient)
{
    __ingredient_delete(ingredient);
}
