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
#include <chef/client.h>
#include <chef/utils_vafs.h>
#include "inventory.h"
#include <libfridge.h>
#include <libplatform.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vafs/vafs.h>
#include <zstd.h>

#define FRIDGE_ROOT_PATH ".fridge"

// In the storage area we store the raw unpacked ingredients. We only unpack
// ingredients when we need them into the prep area
#define FRIDGE_STORAGE_PATH FRIDGE_ROOT_PATH CHEF_PATH_SEPARATOR_S "storage"

// The prep area contains ingredients needed for the recipe.
#define FRIDGE_PREP_PATH FRIDGE_ROOT_PATH CHEF_PATH_SEPARATOR_S "prep"

// The utensils area contains the directory for tools. Each tool will have their
// own subdirectory in the utensils area. A tool can for instance be a toolchain
#define FRIDGE_UTENSILS_PATH FRIDGE_ROOT_PATH CHEF_PATH_SEPARATOR_S "utensils"

struct progress_context {
    int disabled;

    int files;
    int directories;
    int symlinks;

    int files_total;
    int directories_total;
    int symlinks_total;
};

struct VaFsFeatureFilter {
    struct VaFsFeatureHeader Header;
};

static struct fridge_inventory* g_inventory     = NULL;
static struct VaFsGuid          g_headerGuid    = CHEF_PACKAGE_HEADER_GUID;
static struct VaFsGuid          g_overviewGuid  = VA_FS_FEATURE_OVERVIEW;
static struct VaFsGuid          g_filterGuid    = VA_FS_FEATURE_FILTER;
static struct VaFsGuid          g_filterOpsGuid = VA_FS_FEATURE_FILTER_OPS;

static const char* g_rootPath      = NULL;
static const char* g_storagePath   = NULL;
static const char* g_prepPath      = NULL;
static const char* g_utensilsPath  = NULL;

static const char* __get_relative_path(
    const char* root,
    const char* path)
{
    const char* relative = path;
    if (strncmp(path, root, strlen(root)) == 0)
        relative = path + strlen(root);
    return relative;
}

static int __get_cwd(char** bufferOut)
{
    char*  cwd;
    size_t cwdLength;
    int    status;

    cwd = malloc(PATH_MAX);
    if (cwd == NULL) {
        errno = ENOMEM;
        return -1;
    }

    status = platform_getcwd(cwd, PATH_MAX);
    if (status) {
        free(cwd);
        return -1;
    }

    // make sure it ends on a path seperator
    cwdLength = strlen(cwd);
    if (cwd[cwdLength - 1] != CHEF_PATH_SEPARATOR) {
        cwd[cwdLength] = CHEF_PATH_SEPARATOR;
        cwd[cwdLength + 1] = '\0';
    }

    *bufferOut = cwd;
    return 0;
}

static void __write_progress(const char* prefix, struct progress_context* context, int verbose)
{
    static int last = 0;
    int        current;
    int        total;
    int        percent;

    if (context->disabled) {
        return;
    }

    total   = context->files_total + context->directories_total + context->symlinks_total;
    current = context->files + context->directories + context->symlinks;
    percent = (current * 100) / total;

    printf("\33[2K\r%-15.15s [", prefix);
    for (int i = 0; i < 20; i++) {
        if (i < percent / 5) {
            printf("#");
        }
        else {
            printf(" ");
        }
    }
    printf("| %3d%%]", percent);
    if (verbose) {
        if (context->files_total) {
            printf(" %i/%i files", context->files, context->files_total);
        }
        if (context->directories_total) {
            printf(" %i/%i directories", context->directories, context->directories_total);
        }
        if (context->symlinks_total) {
            printf(" %i/%i symlinks", context->symlinks, context->symlinks_total);
        }
    }
    fflush(stdout);
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
    struct progress_context*    progress,
    struct VaFsDirectoryHandle* directoryHandle,
    const char*                 root,
    const char*                 path)
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

        __write_progress(dp.Name, progress, 0);
        if (dp.Type == VaFsEntryType_Directory) {
            struct VaFsDirectoryHandle* subdirectoryHandle;
            status = vafs_directory_open_directory(directoryHandle, dp.Name, &subdirectoryHandle);
            if (status) {
                fprintf(stderr, "__extract_directory: failed to open directory '%s'\n", __get_relative_path(root, filepathBuffer));
                return -1;
            }

            status = __extract_directory(progress, subdirectoryHandle, root, filepathBuffer);
            if (status) {
                fprintf(stderr, "__extract_directory: unable to extract directory '%s'\n", __get_relative_path(root, path));
                return -1;
            }

            status = vafs_directory_close(subdirectoryHandle);
            if (status) {
                fprintf(stderr, "__extract_directory: failed to close directory '%s'\n", __get_relative_path(root, filepathBuffer));
                return -1;
            }
            progress->directories++;
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
            progress->files++;
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
            progress->symlinks++;
        } else {
            fprintf(stderr, "__extract_directory: unable to extract unknown type '%s'\n", __get_relative_path(root, filepathBuffer));
            return -1;
        }
        __write_progress(dp.Name, progress, 0);
        free(filepathBuffer);
    } while(1);

    return 0;
}

static int __make_folders(void)
{
    char* cwd;
    char* rootPath;
    char* storagePath;
    char* prepPath;
    char* utensilsPath;
    int   status;

    status = __get_cwd(&cwd);
    if (status) {
        fprintf(stderr, "__make_folders: failed to get root directory\n");
        return -1;
    }

    rootPath = malloc(strlen(cwd) + strlen(FRIDGE_ROOT_PATH) + 1);
    if (rootPath == NULL) {
        free(cwd);
        fprintf(stderr, "__make_folders: failed to allocate memory for root path\n");
        return -1;
    }

    storagePath = malloc(strlen(cwd) + strlen(FRIDGE_STORAGE_PATH) + 1);
    if (storagePath == NULL) {
        free(cwd);
        free(rootPath);
        fprintf(stderr, "__make_folders: failed to allocate memory for storage path\n");
        return -1;
    }

    prepPath = malloc(strlen(cwd) + strlen(FRIDGE_PREP_PATH) + 1);
    if (prepPath == NULL) {
        free(cwd);
        free(rootPath);
        free(storagePath);
        fprintf(stderr, "__make_folders: failed to allocate memory for prep path\n");
        return -1;
    }

    utensilsPath = malloc(strlen(cwd) + strlen(FRIDGE_UTENSILS_PATH) + 1);
    if (utensilsPath == NULL) {
        free(cwd);
        free(rootPath);
        free(storagePath);
        free(prepPath);
        fprintf(stderr, "__make_folders: failed to allocate memory for utensils path\n");
        return -1;
    }

    sprintf(rootPath, "%s%s", cwd, FRIDGE_ROOT_PATH);
    sprintf(storagePath, "%s%s", cwd, FRIDGE_STORAGE_PATH);
    sprintf(prepPath, "%s%s", cwd, FRIDGE_PREP_PATH);
    sprintf(utensilsPath, "%s%s", cwd, FRIDGE_UTENSILS_PATH);
    free(cwd);

    // update global paths
    g_rootPath = rootPath;
    g_storagePath = storagePath;
    g_prepPath = prepPath;
    g_utensilsPath = utensilsPath;

    status = platform_mkdir(rootPath);
    if (status) {
        fprintf(stderr, "__make_folders: failed to create root directory\n");
        return -1;
    }

    status = platform_mkdir(storagePath);
    if (status) {
        fprintf(stderr, "__make_folders: failed to create storage directory\n");
        return -1;
    }

    status = platform_mkdir(prepPath);
    if (status) {
        fprintf(stderr, "__make_folders: failed to create prep directory\n");
        return -1;
    }

    status = platform_mkdir(utensilsPath);
    if (status) {
        fprintf(stderr, "__make_folders: failed to create utensils directory\n");
        return -1;
    }
    return 0;
}

int fridge_initialize(void)
{
    int status;

    status = __make_folders();
    if (status) {
        fprintf(stderr, "fridge_initialize: failed to create folders\n");
        return -1;
    }

    status = inventory_load(g_storagePath, &g_inventory);
    if (status) {
        fprintf(stderr, "fridge_initialize: failed to load inventory\n");
        return -1;
    }
    return 0;
}

void fridge_cleanup(void)
{
    int status;

    // save inventory if loaded
    if (g_inventory != NULL) {
        status = inventory_save(g_inventory);
        if (status) {
            fprintf(stderr, "fridge_cleanup: failed to save inventory: %i\n", status);
        }
        inventory_free(g_inventory);
        g_inventory = NULL;
    }

    // remove the prep area
    if (g_prepPath != NULL) {
        status = platform_rmdir(g_prepPath);
        if (status) {
            fprintf(stderr, "fridge_cleanup: failed to remove %s\n", g_prepPath);
        }
    }

    // free resources
    if (g_rootPath != NULL) {
        free((void*)g_rootPath);
        g_rootPath = NULL;
    }

    if (g_storagePath != NULL) {
        free((void*)g_storagePath);
        g_storagePath = NULL;
    }

    if (g_prepPath != NULL) {
        free((void*)g_prepPath);
        g_prepPath = NULL;
    }

    if (g_utensilsPath != NULL) {
        free((void*)g_utensilsPath);
        g_utensilsPath = NULL;
    }
}

const char* fridge_get_prep_directory(void)
{
    return g_prepPath;
}

static int __parse_version_string(const char* string, struct chef_version* version)
{
    // parse a version string of format "1.2.3(+tag)"
    // where tag is optional
    char* pointer    = (char*)string;
    char* pointerEnd = strchr(pointer, '.');

    // if '.' was not found, then the revision is provided, so we use that
    if (pointerEnd == NULL) {
        version->major    = 0;
        version->minor    = 0;
        version->patch    = 0;
        version->revision = (int)strtol(pointer, &pointerEnd, 10);
        if (version->revision == 0) {
            errno = EINVAL;
            return -1;
        }

        version->tag = NULL;
        return 0;
    }
    
    // extract first part
    version->major = (int)strtol(pointer, &pointerEnd, 10);
    
    // extract second part
    pointer    = pointerEnd + 1;
    pointerEnd = strchr(pointer, '.');
    if (pointerEnd == NULL) {
        errno = EINVAL;
        return -1;
    }
    version->minor = strtol(pointer, &pointerEnd, 10);
    
    pointer    = pointerEnd + 1;
    pointerEnd = NULL;
    
    // extract the 3rd part, patch
    version->patch = strtol(pointer, &pointerEnd, 10);
    version->tag   = NULL;
    return 0;
}

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

static const char* __get_unpack_path(enum chef_package_type type, const char* packageName)
{
    if (type == CHEF_PACKAGE_TYPE_TOOLCHAIN) {
        char* toolchainPath = strpathcombine(g_utensilsPath, packageName);
        if (toolchainPath && platform_mkdir(toolchainPath)) {
            fprintf(stderr, "__get_unpack_path: failed to create toolchain directory\n");
            free(toolchainPath);
            return NULL;
        }
        return toolchainPath;
    }
    return strdup(g_prepPath);
}

static int __handle_overview(struct VaFs* vafsHandle, struct progress_context* progress)
{
    struct VaFsFeatureOverview* overview;
    int                         status;

    status = vafs_feature_query(vafsHandle, &g_overviewGuid, (struct VaFsFeatureHeader**)&overview);
    if (status) {
        fprintf(stderr, "unmkvafs: failed to query feature overview - %i\n", errno);
        return -1;
    }

    progress->files_total       = overview->Counts.Files;
    progress->directories_total = overview->Counts.Directories;
    progress->symlinks_total    = overview->Counts.Symlinks;
    return 0;
}

static int __fridge_unpack(struct fridge_inventory_pack* pack)
{
    struct VaFs*                vafsHandle;
    struct VaFsDirectoryHandle* directoryHandle;
    struct progress_context     progressContext = { 0 };
    enum chef_package_type      packType;
    int                         status;
    const char*                 unpackPath;
    char                        nameBuffer[512];

    // check our inventory status if we should unpack it again
    if (inventory_pack_is_unpacked(pack) == 1) {
        return 0;
    }

    // get the filename of the package
    status = inventory_pack_filename(pack, &nameBuffer[0], sizeof(nameBuffer));
    if (status) {
        fprintf(stderr, "__fridge_unpack: package path too long!\n");
        return -1;
    }

    status = vafs_open_file(&nameBuffer[0], &vafsHandle);
    if (status) {
        fprintf(stderr, "__fridge_unpack: cannot open vafs image: %s\n", &nameBuffer[0]);
        return -1;
    }

    status = __handle_overview(vafsHandle, &progressContext);
    if (status) {
        vafs_close(vafsHandle);
        fprintf(stderr, "__fridge_unpack: failed to handle image overview\n");
        return -1;
    }

    status = __handle_filter(vafsHandle);
    if (status) {
        vafs_close(vafsHandle);
        fprintf(stderr, "__fridge_unpack: failed to handle image filter\n");
        return -1;
    }

    status = vafs_directory_open(vafsHandle, "/", &directoryHandle);
    if (status) {
        vafs_close(vafsHandle);
        fprintf(stderr, "__fridge_unpack: cannot open root directory: /\n");
        return -1;
    }

    // detect the type of ingredient we are unpacking.
    packType   = __get_pack_type(vafsHandle);
    unpackPath = __get_unpack_path(packType, inventory_pack_name(pack));
    if (unpackPath == NULL) {
        vafs_directory_close(directoryHandle);
        vafs_close(vafsHandle);
        fprintf(stderr, "__fridge_unpack: failed to create unpack path\n");
        return -1;
    }

    status = __extract_directory(&progressContext, directoryHandle, unpackPath, unpackPath);
    if (status != 0) {
        free((void*)unpackPath);
        vafs_directory_close(directoryHandle);
        vafs_close(vafsHandle);
        fprintf(stderr, "__fridge_unpack: unable to extract pack\n");
        return -1;
    }
    printf("\n");

    // awesome, lets mark it unpacked
    if (packType == CHEF_PACKAGE_TYPE_TOOLCHAIN) {
        inventory_pack_set_unpacked(pack);
    }

    free((void*)unpackPath);
    vafs_directory_close(directoryHandle);
    return vafs_close(vafsHandle);
}

static int __cache_ingredient(struct fridge_ingredient* ingredient, struct fridge_inventory_pack** packOut)
{
    struct chef_version           version;
    struct chef_version*          versionPtr = NULL;
    struct fridge_inventory_pack* pack;
    char**                        names;
    int                           namesCount;
    int                           status;
    char                          nameBuffer[256];

    if (g_inventory == NULL) {
        errno = ENOSYS;
        fprintf(stderr, "__cache_ingredient: inventory not loaded\n");
        return -1;
    }

    if (ingredient == NULL) {
        errno = EINVAL;
        return -1;
    }

    // parse the version provided if any
    if (ingredient->version != NULL) {
        status = __parse_version_string(ingredient->version, &version);
        if (status) {
            fprintf(stderr, "__cache_ingredient: failed to parse version '%s'\n", ingredient->version);
            return -1;
        }
        versionPtr = &version;
    }

    // split the publisher/package
    names = strsplit(ingredient->name, '/');
    if (names == NULL) {
        fprintf(stderr, "__cache_ingredient: invalid package naming '%s' (must be publisher/package)\n", ingredient->name);
        return -1;
    }
    
    namesCount = 0;
    while (names[namesCount] != NULL) {
        namesCount++;
    }

    if (namesCount != 2) {
        fprintf(stderr, "__cache_ingredient: invalid package naming '%s' (must be publisher/package)\n", ingredient->name);
        status = -1;
        goto cleanup;
    }

    // check if we have the requested ingredient in store already, otherwise
    // download the ingredient
    status = inventory_get_pack(g_inventory, names[0], names[1],
        ingredient->platform, ingredient->arch, ingredient->channel,
        versionPtr, &pack
    );
    if (status == 0) {
        if (packOut) {
            *packOut = pack;
        }
        goto cleanup;
    }
    
    // it's downloaded, lets add it
    status = inventory_add(g_inventory, names[0], names[1],
        ingredient->platform, ingredient->arch, ingredient->channel,
        versionPtr, &pack
    );
    if (status) {
        fprintf(stderr, "__cache_ingredient: failed to add ingredient\n");
        goto cleanup;
    }

    if (packOut) {
        *packOut = pack;
    }

cleanup:
    strsplit_free(names);
    return status;
}

int fridge_store_ingredient(struct fridge_ingredient* ingredient)
{
    return __cache_ingredient(ingredient, NULL);
}

int fridge_use_ingredient(struct fridge_ingredient* ingredient)
{
    struct fridge_inventory_pack* pack;
    int                           status;

    status = __cache_ingredient(ingredient, &pack);
    if (status) {
        return status;
    }
    return __fridge_unpack(pack);
}

char* fridge_get_utensil_location(const char* ingredient)
{
    char** names;
    int    namesCount;
    char*  path = NULL;

    if (ingredient == NULL) {
        errno = EINVAL;
        return NULL;
    }

    // split the publisher/package
    names = strsplit(ingredient, '/');
    if (names == NULL) {
        fprintf(stderr, "fridge_get_utensil_location: invalid package naming '%s' (must be publisher/package)\n", ingredient);
        return NULL;
    }
    
    namesCount = 0;
    while (names[namesCount] != NULL) {
        namesCount++;
    }

    if (namesCount != 2) {
        fprintf(stderr, "fridge_get_utensil_location: invalid package naming '%s' (must be publisher/package)\n", ingredient);
        goto cleanup;
    }

    path = (char*)malloc(strlen(g_utensilsPath) + strlen(names[1]) + 2);
    if (path == NULL) {
        errno = ENOMEM;
        goto cleanup;
    }

    sprintf(path, "%s" CHEF_PATH_SEPARATOR_S "%s", g_utensilsPath, names[1]);

cleanup:
    strsplit_free(names);
    return path;
}
