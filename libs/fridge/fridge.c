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

#include <chef/platform.h>
#include <chef/package.h>
#include <chef/fridge.h>
#include <errno.h>
#include "inventory.h"
#include <string.h>
#include "store.h"
#include <vlog.h>

struct progress_context {
    struct package* package;
    int             disabled;

    int files;
    int directories;
    int symlinks;
};

struct fridge_context {
    char*                    platform;
    char*                    arch;
    struct fridge_store*     store;
    struct fridge_inventory* inventory;
};

static struct fridge_context g_fridge = { 0 };

int fridge_initialize(struct fridge_parameters* parameters)
{
    int status;

    if (parameters->platform == NULL || parameters->architecture == NULL) {
        VLOG_ERROR("fridge", "fridge_initialize: platform and architecture must be specified\n");
        return -1;
    }

    // initialize the store
    status = fridge_store_load(parameters->platform, parameters->architecture, &parameters->backend, &g_fridge.store);
    if (status) {
        VLOG_ERROR("fridge", "fridge_initialize: failed to initialize store backend\n");
        fridge_cleanup();
        return status;
    }
    return 0;
}

void fridge_cleanup(void)
{
    // Free memory allocated by the inventory
    inventory_free(g_fridge.inventory);

    // Reset data
    memset(&g_fridge, 0, sizeof(struct fridge_context));
}

static int __open_inventory(void)
{
    return inventory_load(chef_dirs_store(), &g_fridge.inventory);
}

static int __close_inventory(void)
{
    int status;

    status = inventory_save(g_fridge.inventory);
    inventory_free(g_fridge.inventory);
    g_fridge.inventory = NULL;
    return status;
}

static const char* __get_package_platform(struct fridge_package* package)
{
    if (package->platform == NULL) {
        return g_fridge.platform;
    }
    return package->platform;
}

static const char* __get_package_arch(struct fridge_package* package)
{
    if (package->arch == NULL) {
        return g_fridge.arch;
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
        VLOG_ERROR("fridge", "__find_package_in_inventory: invalid package naming '%s' (must be publisher/package)\n", name);
        return NULL;
    }

    while (names[namesCount] != NULL) {
        namesCount++;
    }

    if (namesCount != 2) {
        VLOG_ERROR("fridge", "__find_package_in_inventory: invalid package naming '%s' (must be publisher/package)\n", name);
        strsplit_free(names);
        return NULL;
    }
    return names;
}

static int __find_package_in_inventory(struct fridge_package* package, struct fridge_inventory_pack** packOut)
{
    struct fridge_inventory_pack* pack;
    char**                        names;
    int                           status;
    VLOG_DEBUG("fridge", "__find_package_in_inventory(name=%s)\n", package->name);

    if (package == NULL) {
        errno = EINVAL;
        return -1;
    }

    // split the publisher/package
    names = __split_name(package->name);
    if (names == NULL) {
        VLOG_ERROR("fridge", "__find_package_in_inventory: invalid package naming '%s' (must be publisher/package)\n", package->name);
        return -1;
    }

    // check if we have the requested package in store already, otherwise
    // download the package
    VLOG_DEBUG("fridge", "looking up path in inventory\n");
    status = inventory_get_pack(
        g_fridge.inventory,
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


int fridge_ensure_package(struct fridge_package* package)
{
    struct fridge_inventory_pack* pack = NULL;
    int                           revision = 0;
    char**                        names = NULL;
    char*                         path = NULL;
    int                           status;

    // split the publisher/package
    names = __split_name(package->name);
    if (names == NULL) {
        VLOG_ERROR("fridge", "fridge_ensure_package: invalid package naming '%s' (must be publisher/package)\n", package->name);
        return -1;
    }

    status = __find_package_in_inventory(package, &pack);
    if (status == 0) {
        VLOG_DEBUG("fridge", "package %s has already been downloaded\n", package->name);
        goto cleanup;
    }

    status = fridge_store_download(g_fridge.store, package, path, &revision);
    if (status) {
        goto cleanup;
    }

    path = __format_package_path(names[0], names[1], revision);
    if (path == NULL) {
        goto cleanup;
    }

    status = inventory_add(
        g_fridge.inventory,
        path,
        names[0], names[1],
        __get_package_platform(package),
        __get_package_arch(package),
        package->channel,
        revision,
        &pack
    );

cleanup:
    strsplit_free(names);
    free(path);
    return status;
}

int fridge_package_path(struct fridge_package* package, const char** pathOut)
{
    struct fridge_inventory_pack* pack = NULL;
    char**                        names;
    int                           status;

    if (package->revision == 0) {
        VLOG_ERROR("fridge", "fridge_package_path: revision is required\n");
        return -1;
    }

    // split the publisher/package
    names = __split_name(package->name);
    if (names == NULL) {
        VLOG_ERROR("fridge", "fridge_package_path: invalid package naming '%s' (must be publisher/package)\n", package->name);
        return -1;
    }

    status = __find_package_in_inventory(package, &pack);
    if (status) {
        VLOG_ERROR("fridge", "fridge_package_path: package '%s' was not found\n", package->name);
        goto cleanup;
    }

    *pathOut = __format_package_path(names[0], names[1], package->revision);

cleanup:
    strsplit_free(names);
    return status;
}

int fridge_proof_ensure(enum fridge_proof_type keyType, const char* key)
{

}

int fridge_proof_lookup(enum fridge_proof_type keyType, const char* key, void* proof)
{

}
