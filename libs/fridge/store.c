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

int fridge_store_download(
    struct fridge_store*   store,
    struct fridge_package* package,
    const char*            path,
    int*                   revisionDownloaded)
{
    int status;
    VLOG_DEBUG("store", "__store_download()\n");

    if (store->backend.resolve_package == NULL) {
        VLOG_ERROR("store", "__store_download: backend does not support resolving packages\n");
        errno = ENOTSUP;
        return -1;
    }

    status = store->backend.resolve_package(
        package,
        PACKAGE_TEMP_PATH,
        revisionDownloaded
    );
    if (status) {
        VLOG_ERROR("store", "__store_download: failed to download %s\n", package->name);
        return -1;
    }
    
    status = rename(PACKAGE_TEMP_PATH, path);
    if (status) {
        VLOG_ERROR("store", "__store_download: failed to move %s => %s\n", PACKAGE_TEMP_PATH, path);
        return -1;
    }
    return status;
}
