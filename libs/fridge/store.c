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
#include <chef/dirs.h>
#include <chef/platform.h>
#include "inventory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "store.h"
#include <vlog.h>

#define PACKAGE_TEMP_PATH "pack.inprogress"

struct fridge_store {
    char*                       platform;
    char*                       arch;
    struct fridge_store_backend backend;
    struct fridge_inventory*    inventory;
};

static struct fridge_store* __store_new(const char* platform, const char* arch, struct fridge_store_backend* backend)
{
    struct fridge_store* store;
    VLOG_DEBUG("store", "__store_new(platform=%s, arch=%s)\n", platform, arch);

    store = malloc(sizeof(struct fridge_store));
    if (store == NULL) {
        return NULL;
    }
    memset(store, 0, sizeof(struct fridge_store));
    memcpy(&store->backend, backend, sizeof(struct fridge_store_backend));
    store->platform = platform_strdup(platform);
    store->arch = platform_strdup(arch);
    return store;
}

static void __store_delete(struct fridge_store* store)
{
    VLOG_DEBUG("store", "__store_delete()\n");
    if (store == NULL) {
        return;
    }

    free(store->platform);
    free(store->arch);
    free(store);
}

int fridge_store_load(const char* platform, const char* arch, struct fridge_store_backend* backend, struct fridge_store** storeOut)
{
    struct fridge_store* store;
    int                  status;
    VLOG_DEBUG("store", "fridge_store_load(platform=%s, arch=%s)\n", platform, arch);

    store = __store_new(platform, arch, backend);
    if (store == NULL) {
        VLOG_ERROR("store", "fridge_store_load: failed to allocate a new store\n");
        return -1;
    }

    *storeOut = store;
    return 0;
}

int fridge_store_open(struct fridge_store* store)
{
    VLOG_DEBUG("store", "fridge_store_open()\n");
    if (store == NULL) {
        errno = EINVAL;
        return -1;
    }
    return inventory_load(chef_dirs_store(), &store->inventory);
}

int fridge_store_close(struct fridge_store* store)
{
    int status;
    VLOG_DEBUG("store", "fridge_store_close()\n");

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
    VLOG_DEBUG("store", "__package_path(publisher=%s, package=%s, platform=%s, arch=%s, channel=%s)\n",
        publisher, package, platform, arch, channel);

    if (revision == 0) {
        VLOG_ERROR("store", "__package_path: revision is not provided, but is required.\n");
        errno = EINVAL;
        return -1;
    }

    written = snprintf(
        pathBuffer,
        bufferSize - 1,
        "%s" CHEF_PATH_SEPARATOR_S "%s-%s-%s-%s-%s-%i.pack",
        chef_dirs_store(),
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
    int*                 revisionDownloaded,
    char**               pathOut)
{
    int  status;
    int  revision;
    char pathBuffer[2048];
    VLOG_DEBUG("store", "__store_download()\n");

    if (store->backend.resolve_ingredient == NULL) {
        VLOG_ERROR("store", "__store_download: backend does not support resolving ingredients\n");
        errno = ENOTSUP;
        return -1;
    }

    status = store->backend.resolve_ingredient(
        publisher,
        package, 
        platform,
        arch,
        channel,
        version,
        PACKAGE_TEMP_PATH,
        &revision
    );
    if (status) {
        VLOG_ERROR("store", "__store_download: failed to download %s/%s\n", publisher, package);
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
        revision,
        &pathBuffer[0],
        sizeof(pathBuffer)
    );
    if (status) {
        VLOG_ERROR("store", "__store_download: package path too long!\n");
        return -1;
    }

    status = platform_copyfile(PACKAGE_TEMP_PATH, &pathBuffer[0]);
    if (status) {
        VLOG_ERROR("store", "__store_download: failed to copy %s => %s\n", PACKAGE_TEMP_PATH, &pathBuffer[0]);
        return -1;
    }

    status = remove(PACKAGE_TEMP_PATH);
    if (status) {
        VLOG_ERROR("store", "__store_download: failed to remove temporary artifact\n");
        return -1;
    }

    if (pathOut != NULL) {
        *pathOut = platform_strdup(&pathBuffer[0]);
    }
    
    *revisionDownloaded = revision;
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

int fridge_store_find_ingredient(struct fridge_store* store, struct fridge_ingredient* ingredient, struct fridge_inventory_pack** packOut)
{
    struct chef_version           version = { 0 };
    struct chef_version*          versionPtr = NULL;
    struct fridge_inventory_pack* pack;
    char**                        names;
    int                           namesCount;
    int                           status;
    VLOG_DEBUG("store", "fridge_store_ensure_ingredient(name=%s)\n", ingredient->name);

    if (ingredient == NULL) {
        errno = EINVAL;
        return -1;
    }

    // parse the version provided if any
    if (ingredient->version != NULL) {
        status = chef_version_from_string(ingredient->version, &version);
        if (status) {
            VLOG_ERROR("store", "fridge_store_ensure_ingredient: failed to parse version '%s'\n", ingredient->version);
            return -1;
        }
        versionPtr = &version;
    }

    // split the publisher/package
    names = strsplit(ingredient->name, '/');
    if (names == NULL) {
        VLOG_ERROR("store", "fridge_store_ensure_ingredient: invalid package naming '%s' (must be publisher/package)\n", ingredient->name);
        return -1;
    }
    
    namesCount = 0;
    while (names[namesCount] != NULL) {
        namesCount++;
    }

    if (namesCount != 2) {
        VLOG_ERROR("store", "fridge_store_ensure_ingredient: invalid package naming '%s' (must be publisher/package)\n", ingredient->name);
        status = -1;
        goto cleanup;
    }

    // check if we have the requested ingredient in store already, otherwise
    // download the ingredient
    VLOG_DEBUG("store", "looking up path in inventory\n");
    status = inventory_get_pack(
        store->inventory,
        names[0], names[1],
        __get_ingredient_platform(store, ingredient),
        __get_ingredient_arch(store, ingredient),
        ingredient->channel,
        versionPtr,
        &pack
    );

cleanup:
    strsplit_free(names);
    return status;
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
    char*                         packPath;
    VLOG_DEBUG("store", "fridge_store_ensure_ingredient(name=%s)\n", ingredient->name);

    if (ingredient == NULL) {
        errno = EINVAL;
        return -1;
    }

    // parse the version provided if any
    if (ingredient->version != NULL) {
        status = chef_version_from_string(ingredient->version, &version);
        if (status) {
            VLOG_ERROR("store", "fridge_store_ensure_ingredient: failed to parse version '%s'\n", ingredient->version);
            return -1;
        }
        versionPtr = &version;
    }

    // split the publisher/package
    names = strsplit(ingredient->name, '/');
    if (names == NULL) {
        VLOG_ERROR("store", "fridge_store_ensure_ingredient: invalid package naming '%s' (must be publisher/package)\n", ingredient->name);
        return -1;
    }
    
    namesCount = 0;
    while (names[namesCount] != NULL) {
        namesCount++;
    }

    if (namesCount != 2) {
        VLOG_ERROR("store", "fridge_store_ensure_ingredient: invalid package naming '%s' (must be publisher/package)\n", ingredient->name);
        status = -1;
        goto cleanup;
    }

    // check if we have the requested ingredient in store already, otherwise
    // download the ingredient
    VLOG_DEBUG("store", "looking up path in inventory\n");
    status = inventory_get_pack(
        store->inventory,
        names[0], names[1],
        __get_ingredient_platform(store, ingredient),
        __get_ingredient_arch(store, ingredient),
        ingredient->channel,
        versionPtr,
        &pack
    );
    if (status == 0) {
        goto cleanup;
    }
    
    VLOG_DEBUG("store", "downloading pack\n");
    status = __store_download(
        store,
        names[0], names[1],
        __get_ingredient_platform(store, ingredient),
        __get_ingredient_arch(store, ingredient),
        ingredient->channel,
        versionPtr,
        &revision,
        &packPath
    );
    if (status) {
        VLOG_ERROR("store", "fridge_store_ensure_ingredient: failed to download ingredient\n");
        return -1;
    }

    // When adding to inventory the version must not be null,
    // but it does only need to have revision set
    if (versionPtr == NULL) {
        version.revision = revision;
        versionPtr = &version;
    }

    VLOG_DEBUG("store", "registering pack in inventory\n");
    status = inventory_add(
        store->inventory,
        packPath,
        names[0], names[1],
        __get_ingredient_platform(store, ingredient),
        __get_ingredient_arch(store, ingredient),
        ingredient->channel,
        versionPtr,
        &pack
    );
    free(packPath);
    if (status) {
        VLOG_ERROR("store", "fridge_store_ensure_ingredient: failed to add ingredient\n");
        goto cleanup;
    }

cleanup:
    if (packOut) {
        *packOut = pack;
    }
    strsplit_free(names);
    return status;
}
