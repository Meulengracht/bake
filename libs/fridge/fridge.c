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
#include "inventory.h"
#include <libingredient.h>
#include <libfridge.h>
#include <limits.h>
#include <chef/platform.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "store.h"
#include <vafs/vafs.h>
#include <vafs/file.h>
#include <vafs/directory.h>

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
    struct ingredient* ingredient;
    int                disabled;

    int files;
    int directories;
    int symlinks;
};

struct fridge_context {
    struct fridge_inventory* inventory;
    struct fridge_store*     store;
    const char*              root_path;
    const char*              storage_path;
    const char*              prep_path;
    const char*              utensils_path;
};

static struct fridge_context g_fridge = { 0 };

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

    total   = context->ingredient->file_count + context->ingredient->directory_count + context->ingredient->symlink_count;
    current = context->files + context->directories + context->symlinks;
    percent = (current * 100) / total;

    printf("\33[2K\rextracting [");
    for (int i = 0; i < 20; i++) {
        if (i < percent / 5) {
            printf("#");
        }
        else {
            printf(" ");
        }
    }
    printf("| %3d%%] %-15.15s", percent, prefix);
    if (verbose) {
        if (context->ingredient->file_count) {
            printf(" %i/%i files", context->files, context->ingredient->file_count);
        }
        if (context->ingredient->directory_count) {
            printf(" %i/%i directories", context->directories, context->ingredient->directory_count);
        }
        if (context->ingredient->symlink_count) {
            printf(" %i/%i symlinks", context->symlinks, context->ingredient->symlink_count);
        }
    }
    fflush(stdout);
}

static int __zip_file(
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

            status = __zip_file(fileHandle, filepathBuffer);
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
    char* storePath;
    int   status;

    status = __get_cwd(&cwd);
    if (status) {
        fprintf(stderr, "__make_folders: failed to get root directory\n");
        return status;
    }

    status = __get_store_path(&storePath);
    if (status) {
        fprintf(stderr, "__make_folders: failed to get global store directory\n");
        free(cwd);
        return status;
    }

    // update global paths
    g_fridge.root_path     = strpathcombine(cwd, FRIDGE_ROOT_PATH);
    g_fridge.storage_path  = strpathcombine(cwd, FRIDGE_STORAGE_PATH);
    g_fridge.prep_path     = strpathcombine(cwd, FRIDGE_PREP_PATH);
    g_fridge.utensils_path = strpathcombine(cwd, FRIDGE_UTENSILS_PATH);
    free(cwd);
    if (g_fridge.root_path == NULL || g_fridge.storage_path == NULL || g_fridge.prep_path == NULL || g_fridge.utensils_path == NULL) {
        fprintf(stderr, "__make_folders: unable to allocate memory for paths\n");
        return -1;
    }

    status = platform_mkdir(g_fridge.root_path);
    if (status) {
        fprintf(stderr, "__make_folders: failed to create root directory\n");
        return -1;
    }

    status = platform_mkdir(g_fridge.storage_path);
    if (status) {
        fprintf(stderr, "__make_folders: failed to create storage directory\n");
        return -1;
    }

    status = platform_mkdir(g_fridge.prep_path);
    if (status) {
        fprintf(stderr, "__make_folders: failed to create prep directory\n");
        return -1;
    }

    status = platform_mkdir(g_fridge.utensils_path);
    if (status) {
        fprintf(stderr, "__make_folders: failed to create utensils directory\n");
        return -1;
    }
    return 0;
}

int fridge_initialize(const char* platform, const char* architecture)
{
    int status;

    if (platform == NULL || architecture == NULL) {
        fprintf(stderr, "fridge_initialize: platform and architecture must be specified\n");
        return -1;
    }

    status = __make_folders();
    if (status) {
        fprintf(stderr, "fridge_initialize: failed to create folders\n");
        fridge_cleanup();
        return -1;
    }

    // initialize the store inventory
    status = fridge_store_load(platform, architecture, &g_fridge.store);
    if (status) {
        fprintf(stderr, "fridge_initialize: failed to load store inventory\n");
        fridge_cleanup();
        return -1;
    }

    status = inventory_load(g_fridge.storage_path, &g_fridge.inventory);
    if (status) {
        fprintf(stderr, "fridge_initialize: failed to load inventory\n");
        fridge_cleanup();
        return -1;
    }
    return 0;
}

void fridge_cleanup(void)
{
    int status;

    // save inventory if loaded
    if (g_fridge.inventory != NULL) {
        status = inventory_save(g_fridge.inventory);
        if (status) {
            fprintf(stderr, "fridge_cleanup: failed to save inventory: %i\n", status);
        }
        inventory_free(g_fridge.inventory);
    }

    // remove the prep area
    if (g_fridge.prep_path != NULL) {
        status = platform_rmdir(g_fridge.prep_path);
        if (status) {
            fprintf(stderr, "fridge_cleanup: failed to remove %s\n", g_fridge.prep_path);
        }
    }

    // free resources
    free((void*)g_fridge.root_path);
    free((void*)g_fridge.storage_path);
    free((void*)g_fridge.prep_path);
    free((void*)g_fridge.utensils_path);

    // reset context
    memset(&g_fridge, 0, sizeof(struct fridge_context));
}

const char* fridge_get_prep_directory(void)
{
    return g_fridge.prep_path;
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

static const char* __get_unpack_path(enum chef_package_type type, const char* packageName)
{
    if (type == CHEF_PACKAGE_TYPE_TOOLCHAIN) {
        char* toolchainPath = strpathcombine(g_fridge.utensils_path, packageName);
        if (toolchainPath && platform_mkdir(toolchainPath)) {
            fprintf(stderr, "__get_unpack_path: failed to create toolchain directory\n");
            free(toolchainPath);
            return NULL;
        }
        return toolchainPath;
    }
    return strdup(g_fridge.prep_path);
}

static int __fridge_unpack(struct fridge_inventory_pack* pack)
{
    struct ingredient*      ingredient;
    struct progress_context progressContext = { 0 };
    int                     status;
    const char*             unpackPath;
    const char*             packPath;

    // check our inventory status if we should unpack it again
    if (inventory_pack_is_unpacked(pack) == 1) {
        return 0;
    }

    // get the filename of the package
    packPath = inventory_pack_filename(pack);
    status   = ingredient_open(packPath, &ingredient);
    if (status) {
        fprintf(stderr, "__fridge_unpack: cannot open ingredient: %s\n", packPath);
        return -1;
    }

    unpackPath = __get_unpack_path(ingredient->type, inventory_pack_name(pack));
    if (unpackPath == NULL) {
        ingredient_close(ingredient);
        fprintf(stderr, "__fridge_unpack: failed to create unpack path\n");
        return -1;
    }

    // store the ingredient used for progress calculation
    progressContext.ingredient = ingredient;

    status = __extract_directory(&progressContext, ingredient->root_handle, unpackPath, unpackPath);
    if (status != 0) {
        free((void*)unpackPath);
        ingredient_close(ingredient);
        fprintf(stderr, "__fridge_unpack: unable to extract pack\n");
        return -1;
    }
    printf("\n");

    // awesome, lets mark it unpacked
    if (ingredient->type == CHEF_PACKAGE_TYPE_TOOLCHAIN) {
        inventory_pack_set_unpacked(pack);
    }

    ingredient_close(ingredient);
    free((void*)unpackPath);
    return 0;
}

int fridge_store_ingredient(struct fridge_ingredient* ingredient)
{
    int status;

    status = fridge_store_open(g_fridge.store);
    if (status) {
        return status;
    }

    status = fridge_store_ensure_ingredient(g_fridge.store, ingredient, NULL);
    if (status) {
        (void)fridge_store_close(g_fridge.store);
        return status;
    }
    return fridge_store_close(g_fridge.store);
}

static int __ensure_ingredient(struct fridge_ingredient* ingredient, struct fridge_inventory_pack** packOut)
{
    struct chef_version           version = { 0 };
    struct chef_version*          versionPtr = NULL;
    struct fridge_inventory_pack* pack;
    char**                        names;
    int                           namesCount;
    int                           status;
    int                           revision;
    struct fridge_inventory*      inventory;

    if (ingredient == NULL) {
        errno = EINVAL;
        return -1;
    }

    // parse the version provided if any
    if (ingredient->version != NULL) {
        status = __parse_version_string(ingredient->version, &version);
        if (status) {
            fprintf(stderr, "__ensure_ingredient: failed to parse version '%s'\n", ingredient->version);
            return -1;
        }
        versionPtr = &version;
    }

    // split the publisher/package
    names = strsplit(ingredient->name, '/');
    if (names == NULL) {
        fprintf(stderr, "__ensure_ingredient: invalid package naming '%s' (must be publisher/package)\n", ingredient->name);
        return -1;
    }
    
    namesCount = 0;
    while (names[namesCount] != NULL) {
        namesCount++;
    }

    if (namesCount != 2) {
        fprintf(stderr, "__ensure_ingredient: invalid package naming '%s' (must be publisher/package)\n", ingredient->name);
        status = -1;
        goto cleanup;
    }

    // check if we have the requested ingredient in store already, otherwise
    // download the ingredient
    status = inventory_get_pack(
        inventory,
        names[0], names[1],
        __get_ingredient_platform(store, ingredient),
        __get_ingredient_arch(store, ingredient),
        ingredient->channel,
        versionPtr,
        &pack
    );
    if (status == 0) {
        if (packOut) {
            *packOut = pack;
        }
        goto cleanup;
    }
    
    // When adding to inventory the version must not be null,
    // but it does only need to have revision set
    if (versionPtr == NULL) {
        version.revision = revision;
        versionPtr = &version;
    }

    status = inventory_add(
        inventory,
        PACKAGE_TEMP_PATH,
        names[0], names[1],
        __get_ingredient_platform(store, ingredient),
        __get_ingredient_arch(store, ingredient),
        ingredient->channel,
        versionPtr,
        &pack
    );
    if (status) {
        fprintf(stderr, "fridge_store_ensure_ingredient: failed to add ingredient\n");
        goto cleanup;
    }

    if (packOut) {
        *packOut = pack;
    }

cleanup:
    strsplit_free(names);
    return status;
}


int fridge_use_ingredient(struct fridge_ingredient* ingredient)
{
    struct fridge_inventory_pack* pack;
    int                           status;
    char                          packPath[512];

    status = fridge_store_open(g_fridge.store);
    if (status) {
        return status;
    }

    status = fridge_store_ensure_ingredient(g_fridge.store, ingredient, &pack);
    if (status) {
        (void)fridge_store_close(g_fridge.store);
        return status;
    }

    status = inventory_pack_filename()

    return fridge_store_close(g_fridge.store);


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

    path = (char*)malloc(strlen(g_fridge.utensils_path) + strlen(names[1]) + 2);
    if (path == NULL) {
        errno = ENOMEM;
        goto cleanup;
    }

    sprintf(path, "%s" CHEF_PATH_SEPARATOR_S "%s", g_fridge.utensils_path, names[1]);

cleanup:
    strsplit_free(names);
    return path;
}
