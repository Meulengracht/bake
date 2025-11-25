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

#include <chef/dirs.h>
#include <chef/platform.h>
#include <chef/package.h>
#include <chef/store.h>
#include <errno.h>
#include "inventory.h"
#include <stdlib.h>
#include <string.h>
#include <vlog.h>

struct progress_context {
    struct package* package;
    int             disabled;

    int files;
    int directories;
    int symlinks;
};

struct store_context {
    char*                   platform;
    char*                   arch;
    struct store_backend    backend;
    struct store_inventory* inventory;
};

static struct store_context g_store = { 0 };

int store_initialize(struct store_parameters* parameters)
{
    int status;
    VLOG_DEBUG("store", "store_initialize()\n");

    if (parameters->platform == NULL || parameters->architecture == NULL) {
        VLOG_ERROR("store", "store_initialize: platform and architecture must be specified\n");
        return -1;
    }

    memcpy(&g_store.backend, &parameters->backend, sizeof(struct store_backend));
    g_store.arch = platform_strdup(parameters->architecture);
    g_store.platform = platform_strdup(parameters->platform);

    status = inventory_load(chef_dirs_store(), &g_store.inventory);
    if (status) {
        store_cleanup();
        return -1;
    }
    return status;
}

void store_cleanup(void)
{
    VLOG_DEBUG("store", "store_cleanup()\n");

    // Free memory allocated by the inventory
    inventory_free(g_store.inventory);
    free(g_store.platform);
    free(g_store.arch);

    // Reset data
    memset(&g_store, 0, sizeof(struct store_context));
}

static const char* __get_package_platform(struct store_package* package)
{
    if (package->platform == NULL) {
        return g_store.platform;
    }
    return package->platform;
}

static const char* __get_package_arch(struct store_package* package)
{
    if (package->arch == NULL) {
        return g_store.arch;
    }
    return package->arch;
}

static char* __format_package_path(
    const char* publisher,
    const char* package,
    int         revision)
{
    char buffer[PATH_MAX];
    snprintf(
        &buffer[0], sizeof(buffer) - 1,
        "%s" CHEF_PATH_SEPARATOR_S "%s-%s-%i.pack",
        chef_dirs_store(),
        publisher,
        package,
        revision
    );
    return platform_strdup(&buffer[0]);
}

static char** __split_name(const char* name)
{
    // split the publisher/package
    int    namesCount = 0;
    char** names = strsplit(name, '/');
    if (names == NULL) {
        VLOG_ERROR("store", "__find_package_in_inventory: invalid package naming '%s' (must be publisher/package)\n", name);
        return NULL;
    }

    while (names[namesCount] != NULL) {
        namesCount++;
    }

    if (namesCount != 2) {
        VLOG_ERROR("store", "__find_package_in_inventory: invalid package naming '%s' (must be publisher/package)\n", name);
        strsplit_free(names);
        return NULL;
    }
    return names;
}

static int __find_package_in_inventory(struct store_package* package, struct store_inventory_pack** packOut)
{
    struct store_inventory_pack* pack;
    char**                       names;
    int                          status;
    VLOG_DEBUG("store", "__find_package_in_inventory(name=%s)\n", package->name);

    if (package == NULL) {
        errno = EINVAL;
        return -1;
    }

    // split the publisher/package
    names = __split_name(package->name);
    if (names == NULL) {
        VLOG_ERROR("store", "__find_package_in_inventory: invalid package naming '%s' (must be publisher/package)\n", package->name);
        return -1;
    }

    // check if we have the requested package in store already, otherwise
    // download the package
    VLOG_DEBUG("store", "looking up path in inventory\n");
    status = inventory_get_pack(
        g_store.inventory,
        names[0], names[1],
        __get_package_platform(package),
        __get_package_arch(package),
        package->channel,
        package->revision,
        &pack
    );

cleanup:
    strsplit_free(names);
    return status;
}

int store_ensure_package(struct store_package* package, struct chef_observer* observer)
{
    struct store_inventory_pack* pack = NULL;
    int                          revision = 0;
    char**                       names = NULL;
    char*                        path = NULL;
    char*                        pathTmp = NULL;
    int                          status;
    VLOG_DEBUG("store", "store_ensure_package(name=%s)\n", package->name);

    if (g_store.backend.resolve_package == NULL) {
        errno = ENOTSUP;
        return -1;
    }

    // split the publisher/package
    names = __split_name(package->name);
    if (names == NULL) {
        VLOG_ERROR("store", "store_ensure_package: invalid package naming '%s' (must be publisher/package)\n", package->name);
        return -1;
    }

    status = __find_package_in_inventory(package, &pack);
    if (status == 0) {
        VLOG_DEBUG("store", "package %s has already been downloaded\n", package->name);
        goto cleanup;
    }

    // The issue is we cannot know the revision until we resolve the package
    // and thus we must download the package to a temporary filename first
    pathTmp = __format_package_path(names[0], names[1], 0);
    if (pathTmp == NULL) {
        goto cleanup;
    }

    status = g_store.backend.resolve_package(package, pathTmp, observer, &revision);
    if (status) {
        goto cleanup;
    }

    path = __format_package_path(names[0], names[1], revision);
    if (path == NULL) {
        goto cleanup;
    }

    status = rename(pathTmp, path);
    if (status) {
        goto cleanup;
    }

    status = inventory_add(
        g_store.inventory,
        path,
        names[0], names[1],
        __get_package_platform(package),
        __get_package_arch(package),
        package->channel,
        revision,
        &pack
    );
    if (status) {
        goto cleanup;
    }

    status = inventory_save(g_store.inventory);

cleanup:
    strsplit_free(names);
    free(pathTmp);
    free(path);
    return status;
}

int store_package_path(struct store_package* package, const char** pathOut)
{
    struct store_inventory_pack* pack = NULL;
    char**                       names;
    int                          status;
    VLOG_DEBUG("store", "store_package_path(name=%s)\n", package->name);

    if (package->revision == 0) {
        VLOG_ERROR("store", "store_package_path: revision is required\n");
        return -1;
    }

    // split the publisher/package
    names = __split_name(package->name);
    if (names == NULL) {
        VLOG_ERROR("store", "store_package_path: invalid package naming '%s' (must be publisher/package)\n", package->name);
        return -1;
    }

    status = __find_package_in_inventory(package, &pack);
    if (status) {
        VLOG_ERROR("store", "store_package_path: package '%s' was not found\n", package->name);
        goto cleanup;
    }

    *pathOut = __format_package_path(names[0], names[1], package->revision);

cleanup:
    strsplit_free(names);
    return status;
}

int store_proof_ensure(enum store_proof_type keyType, const char* key, struct chef_observer* observer)
{
    union store_proof proof;
    int               status;
    VLOG_DEBUG("store", "store_proof_ensure(key=%s)\n", key);

    if (g_store.backend.resolve_proof == NULL) {
        errno = ENOTSUP;
        return -1;
    }

    status = store_proof_lookup(keyType, key, &proof);
    if (status == 0) {
        VLOG_DEBUG("store", "proof %s has already been downloaded\n", key);
        goto cleanup;
    }

    status = g_store.backend.resolve_proof(keyType, key, observer, &proof);
    if (status) {
        goto cleanup;
    }

    status = inventory_add_proof(g_store.inventory, &proof);
    if (status) {
        goto cleanup;
    }

    status = inventory_save(g_store.inventory);

cleanup:
    return status;
}

int store_proof_lookup(enum store_proof_type keyType, const char* key, void* proof)
{
    VLOG_DEBUG("store", "store_proof_lookup(key=%s)\n", key);
    return inventory_get_proof(g_store.inventory, keyType, key, proof);
}
