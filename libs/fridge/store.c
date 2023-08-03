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
#include <chef/api/package.h>
#include <chef/client.h>
#include <chef/platform.h>
#include "inventory.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "store.h"

#define PACKAGE_TEMP_PATH "pack.inprogress"

struct fridge_store {
    char*                    path;
    char*                    platform;
    char*                    arch;
    struct fridge_inventory* inventory;
};

static int __get_store_path(char** pathOut)
{
    char* path;
    int   status;

    path = malloc(PATH_MAX);
    if (path == NULL) {
        return -1;
    }

    status = platform_getuserdir(path, PATH_MAX);
    if (status != 0) {
        free(path);
        return -1;
    }

    strcat(path, CHEF_PATH_SEPARATOR_S ".chef" CHEF_PATH_SEPARATOR_S "store");
    *pathOut = path;
    return 0;
}

static struct fridge_store* __store_new(const char* platform, const char* arch)
{
    struct fridge_store* store;

    store = malloc(sizeof(struct fridge_store));
    if (store == NULL) {
        return NULL;
    }
    memset(store, 0, sizeof(struct fridge_store));
    store->platform = strdup(platform);
    store->arch = strdup(arch);
    return store;
}

static void __store_delete(struct fridge_store* store)
{
    if (store == NULL) {
        return;
    }

    free(store->path);
    free(store->platform);
    free(store->arch);
    free(store);
}

int fridge_store_load(const char* platform, const char* arch, struct fridge_store** storeOut)
{
    struct fridge_store* store;
    int                  status;

    store = __store_new(platform, arch);
    if (store == NULL) {
        return -1;
    }

    status = __get_store_path(&store->path);
    if (status) {
        fprintf(stderr, "fridge_store_load: failed to get global store directory\n");
        __store_delete(store);
        return status;
    }

    status = platform_mkdir(store->path);
    if (status) {
        fprintf(stderr, "fridge_store_load: failed to create global store directory\n");
        return -1;
    }

    *storeOut = store;
    return 0;
}

int fridge_store_open(struct fridge_store* store)
{
    if (store == NULL) {
        errno = EINVAL;
        return -1;
    }
    return inventory_load(store->path, &store->inventory);
}

int fridge_store_close(struct fridge_store* store)
{
    int status;

    if (store == NULL) {
        errno = EINVAL;
        return -1;
    }

    status = inventory_save(store->inventory);
    inventory_free(store->inventory);
    store->inventory = NULL;
    return status;
}

static int __package_path(
    struct fridge_store* store,
    const char*          publisher,
    const char*          package,
    const char*          platform,
    const char*          arch,
    const char*          channel,
    int                  revision,
    char*                pathBuffer,
    size_t               bufferSize)
{
    int written;

    if (revision == 0) {
        fprintf(stderr, "__package_path: revision is not provided, but is required.\n");
        errno = EINVAL;
        return -1;
    }

    written = snprintf(
        pathBuffer,
        bufferSize - 1,
        "%s" CHEF_PATH_SEPARATOR_S "%s-%s-%s-%s-%s-%i.pack",
        store->path,
        publisher,
        package,
        platform,
        arch,
        channel,
        revision
    );
    if (written == bufferSize - 1) {
        errno = ERANGE;
        return -1;
    }
    if (written < 0) {
        return -1;
    }
    return 0;
}

static int __store_download(
    struct fridge_store* store,
    const char*          publisher,
    const char*          package,
    const char*          platform,
    const char*          arch,
    const char*          channel,
    struct chef_version* version,
    int*                 revisionDownloaded)
{
    struct chef_download_params downloadParams;
    int                         status;
    char                        pathBuffer[512];

    // initialize download params
    downloadParams.publisher = publisher;
    downloadParams.package   = package;
    downloadParams.platform  = platform;
    downloadParams.arch      = arch;
    downloadParams.channel   = channel;
    downloadParams.version   = version; // may be null, will just get latest

    status = chefclient_pack_download(&downloadParams, PACKAGE_TEMP_PATH);
    if (status) {
        fprintf(stderr, "__inventory_download: failed to download %s/%s\n", publisher, package);
        return -1;
    }

    // move the package into the right place
    status = __package_path(
        store,
        publisher,
        package,
        platform,
        arch,
        channel,
        downloadParams.revision,
        &pathBuffer[0],
        sizeof(pathBuffer)
    );
    if (status) {
        fprintf(stderr, "__inventory_download: package path too long!\n");
        return -1;
    }
    
    status = rename(PACKAGE_TEMP_PATH, &pathBuffer[0]);
    if (status) {
        fprintf(stderr, "inventory_add: failed to move pack into inventory!\n");
        return -1;
    }
    
    *revisionDownloaded = downloadParams.revision;
    return status;
}

static const char* __get_ingredient_platform(struct fridge_store* store, struct fridge_ingredient* ingredient)
{
    if (ingredient->platform == NULL) {
        return store->platform;
    }
    return ingredient->platform;
}

static const char* __get_ingredient_arch(struct fridge_store* store, struct fridge_ingredient* ingredient)
{
    if (ingredient->arch == NULL) {
        return store->arch;
    }
    return ingredient->arch;
}

int fridge_store_ensure_ingredient(struct fridge_store* store, struct fridge_ingredient* ingredient, struct fridge_inventory_pack** packOut)
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
        status = chef_version_from_string(ingredient->version, &version);
        if (status) {
            fprintf(stderr, "fridge_store_ensure_ingredient: failed to parse version '%s'\n", ingredient->version);
            return -1;
        }
        versionPtr = &version;
    }

    // split the publisher/package
    names = strsplit(ingredient->name, '/');
    if (names == NULL) {
        fprintf(stderr, "fridge_store_ensure_ingredient: invalid package naming '%s' (must be publisher/package)\n", ingredient->name);
        return -1;
    }
    
    namesCount = 0;
    while (names[namesCount] != NULL) {
        namesCount++;
    }

    if (namesCount != 2) {
        fprintf(stderr, "fridge_store_ensure_ingredient: invalid package naming '%s' (must be publisher/package)\n", ingredient->name);
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
    
    status = __store_download(
        store,
        names[0], names[1],
        __get_ingredient_platform(store, ingredient),
        __get_ingredient_arch(store, ingredient),
        ingredient->channel,
        versionPtr,
        &revision
    );
    if (status) {
        return -1;
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
