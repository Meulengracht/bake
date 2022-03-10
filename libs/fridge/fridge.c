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
#include <libfridge.h>
#include <libplatform.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vafs/vafs.h>
#include <zstd.h>

struct VaFsFeatureFilter {
    struct VaFsFeatureHeader Header;
};

static struct fridge_inventory* g_inventory     = NULL;
static struct VaFsGuid          g_filterGuid    = VA_FS_FEATURE_FILTER;
static struct VaFsGuid          g_filterOpsGuid = VA_FS_FEATURE_FILTER_OPS;

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
        if (platform_mkdir(path)) {
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

// we want to create the following folders
// .fridge/storage/packs
// .fridge/prep/
static int __make_folders(void)
{
    if (platform_mkdir(".fridge")) {
        fprintf(stderr, "unmkvafs: failed to create .fridge folder\n");
        return -1;
    }

    if (platform_mkdir(".fridge/storage")) {
        fprintf(stderr, "unmkvafs: failed to create .fridge/storage folder\n");
        return -1;
    }

    if (platform_mkdir(".fridge/prep")) {
        fprintf(stderr, "unmkvafs: failed to create .fridge/prep folder\n");
        return -1;
    }
    return 0;
}

/*
$ mkdir -p ~/local/share
$ cat << EOF > ~/local/share/config.site
CPPFLAGS=-I$HOME/local/include
LDFLAGS=-L$HOME/local/lib
...
EOF
*/

int fridge_initialize(void)
{
    int status;

    status = __make_folders();
    if (status) {
        fprintf(stderr, "fridge_initialize: failed to create folders\n");
        return -1;
    }

    status = inventory_load(".fridge/storage/inventory.json", &g_inventory);
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
        status = inventory_save(g_inventory, ".fridge/storage/inventory.json");
        if (status) {
            fprintf(stderr, "fridge_cleanup: failed to save inventory: %i\n", status);
        }
    }

    // remove the prep area
    status = platform_rmdir(".fridge/prep");
    if (status) {
        fprintf(stderr, "fridge_cleanup: failed to remove prep area\n");
    }
}

static int __parse_version_string(const char* string, struct chef_version* version)
{
    // parse a version string of format "1.2.3(+tag)"
    // where tag is optional
    char* pointer    = (char*)string;
	char* pointerEnd = strchr(pointer, '.');
	if (pointerEnd == NULL) {
	    return -1;
	}
	
	// extract first part
    version->major = (int)strtol(pointer, &pointerEnd, 10);
    
    pointer    = pointerEnd + 1;
	pointerEnd = strchr(pointer, '.');
	if (pointerEnd == NULL) {
	    return -1;
	}
	
	// extract second part
    version->minor = strtol(pointer, &pointerEnd, 10);
    
    pointer    = pointerEnd + 1;
	pointerEnd = strchr(pointer, '+');
    
	// extract the 3rd part, revision
	// at this point, if pointerEnd is not NULL, then it contains tag
	if (pointerEnd != NULL) {
        version->tag = pointerEnd;
	}

	version->revision = strtol(pointer, &pointerEnd, 10);
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
    return 0;
}

static int __set_filter_ops(
    struct VaFs*              vafs,
    struct VaFsFeatureFilter* filter)
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
    return __set_filter_ops(vafs, filter);
}

static int __fridge_unpack(const char* packPath)
{
    struct VaFsDirectoryHandle* directoryHandle;
    struct VaFs*                vafsHandle;
    int                         status;

    status = vafs_open_file(packPath, &vafsHandle);
    if (status) {
        fprintf(stderr, "__fridge_unpack: cannot open vafs image: %s\n", packPath);
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

    status = __extract_directory(directoryHandle, ".fridge/prep", ".fridge/prep");
    if (status != 0) {
        vafs_close(vafsHandle);
        fprintf(stderr, "__fridge_unpack: unable to extract pack\n");
        return -1;
    }

    return vafs_close(vafsHandle);
}

int fridge_store_ingredient(struct fridge_ingredient* ingredient)
{
    struct chef_download_params downloadParams;
    struct chef_version         version;
    struct chef_version*        versionPtr = NULL;
    int                         latest = 0;
    char**                      names;
    int                         namesCount;
    int                         status;
    char                        nameBuffer[256];

    if (g_inventory == NULL) {
        errno = ENOSYS;
        fprintf(stderr, "fridge_store_ingredient: inventory not loaded\n");
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
            fprintf(stderr, "fridge_store_ingredient: failed to parse version '%s'\n", ingredient->version);
            return -1;
        }
        versionPtr = &version;
    }
    else {
        // if no version provided, we want the latest
        latest = 1;
    }

    // split the publisher/package
    names = strsplit(ingredient->name, '/');
    if (names == NULL) {
        fprintf(stderr, "fridge_store_ingredient: invalid package naming '%s' (must be publisher/package)\n", ingredient->name);
        return -1;
    }
    
    namesCount = 0;
    while (names[namesCount] != NULL) {
        namesCount++;
    }

    if (namesCount != 2) {
        fprintf(stderr, "fridge_store_ingredient: invalid package naming '%s' (must be publisher/package)\n", ingredient->name);
        return -1;
    }

    // check if we have the requested ingredient in store already
    status = inventory_contains(g_inventory, names[0], names[1],
        ingredient->platform, ingredient->arch, ingredient->channel,
        versionPtr, latest
    );
    if (status == 0) {
        goto cleanup;
    }

    // otherwise we add the ingredient and download it
    status = inventory_add(g_inventory, names[0], names[1],
        ingredient->platform, ingredient->arch, ingredient->channel,
        versionPtr, latest
    );
    if (status) {
        fprintf(stderr, "fridge_store_ingredient: failed to add ingredient\n");
        goto cleanup;
    }

    // generate the file name
    snprintf(
        nameBuffer, sizeof(nameBuffer) - 1, 
        ".fridge/storage/%s-%s-%s-%s-%s-%s.pack", 
        names[0], names[1],
        ingredient->platform,
        ingredient->arch,
        ingredient->channel, 
        (ingredient->version == NULL ? "latest" : ingredient->version)
    );

    // initialize download params
    downloadParams.publisher = names[0];
    downloadParams.package   = names[1];
    downloadParams.platform  = ingredient->platform;
    downloadParams.arch      = ingredient->arch;
    downloadParams.channel   = ingredient->channel;
    downloadParams.version   = ingredient->version;
    status = chefclient_pack_download(&downloadParams, &nameBuffer[0]);
    if (status) {
        fprintf(stderr, "fridge_use_ingredient: failed to download ingredient %s\n", ingredient->name);
    }

cleanup:
    strsplit_free(names);
    return status;
}

int fridge_use_ingredient(struct fridge_ingredient* ingredient)
{
    struct chef_download_params downloadParams;
    struct chef_version         version;
    struct chef_version*        versionPtr = NULL;
    int                         latest = 0;
    int                         status;
    char**                      names;
    int                         namesCount;
    char                        nameBuffer[256];

    if (g_inventory == NULL) {
        errno = ENOSYS;
        fprintf(stderr, "fridge_use_ingredient: inventory not loaded\n");
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
            fprintf(stderr, "fridge_use_ingredient: failed to parse version '%s'\n", ingredient->version);
            return -1;
        }
        versionPtr = &version;
    }
    else {
        // if no version provided, we want the latest
        latest = 1;
    }

    // split the publisher/package
    names = strsplit(ingredient->name, '/');
    if (names == NULL) {
        fprintf(stderr, "fridge_store_ingredient: invalid package naming '%s' (must be publisher/package)\n", ingredient->name);
        return -1;
    }
    
    namesCount = 0;
    while (names[namesCount] != NULL) {
        namesCount++;
    }

    if (namesCount != 2) {
        fprintf(stderr, "fridge_store_ingredient: invalid package naming '%s' (must be publisher/package)\n", ingredient->name);
        return -1;
    }

    // check if we have the requested ingredient in store already
    status = inventory_contains(g_inventory, names[0], names[1],
        ingredient->platform, ingredient->arch, ingredient->channel,
        versionPtr, latest
    );
    if (status == 0) {
        goto cleanup;
    }

    // otherwise we add the ingredient and download it
    status = inventory_add(g_inventory, names[0], names[1],
        ingredient->platform, ingredient->arch, ingredient->channel,
        versionPtr, latest
    );
    if (status) {
        fprintf(stderr, "fridge_use_ingredient: failed to add ingredient\n");
        goto cleanup;
    }

    // generate the file name
    snprintf(
        nameBuffer, sizeof(nameBuffer) - 1, 
        ".fridge/storage/%s-%s-%s-%s-%s-%s.pack", 
        names[0], names[1],
        ingredient->platform,
        ingredient->arch,
        ingredient->channel,
        (ingredient->version == NULL ? "latest" : ingredient->version)
    );

    // initialize download params
    downloadParams.publisher = names[0];
    downloadParams.package   = names[1];
    downloadParams.platform  = ingredient->platform;
    downloadParams.arch      = ingredient->arch;
    downloadParams.channel   = ingredient->channel;
    downloadParams.version   = ingredient->version;
    status = chefclient_pack_download(&downloadParams, &nameBuffer[0]);
    if (status) {
        fprintf(stderr, "fridge_use_ingredient: failed to download ingredient %s\n", ingredient->name);
        goto cleanup;
    }

    // unpack it into preparation area
    status = __fridge_unpack(nameBuffer);
    if (status) {
        fprintf(stderr, "fridge_use_ingredient: failed to unpack ingredient %s\n", ingredient->name);
    }

cleanup:
    strsplit_free(names);
    return status;
}
