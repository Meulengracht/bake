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
#include <vlog.h>

struct progress_context {
    struct ingredient* ingredient;
    int                disabled;

    int files;
    int directories;
    int symlinks;
};

struct fridge_context {
    char*                root;
    struct fridge_store* store;
};

static struct fridge_context g_fridge = { 0 };

static int __make_folders(const char* root)
{
    int status;

    status = platform_mkdir(root);
    if (status) {
        VLOG_ERROR("fridge", "__make_folders: failed to create root directory\n");
        return -1;
    }
    
    return 0;
}

int fridge_initialize(const char* platform, const char* architecture)
{
    int   status;
    char  temp[2048] = { 0 };
    char* root;

    if (platform == NULL || architecture == NULL) {
        VLOG_ERROR("fridge", "fridge_initialize: platform and architecture must be specified\n");
        return -1;
    }

    status = platform_getuserdir(&temp[0], sizeof(temp) - 1);
    if (status) {
        VLOG_ERROR("fridge", "fridge_initialize: failed to resolve user homedir\n");
        return -1;
    }

    g_fridge.root = strpathjoin(&temp[0], ".chef", "fridge", NULL);
    if (g_fridge.root == NULL) {
        VLOG_ERROR("fridge", "fridge_initialize: failed to allocate memory for root directory\n");
        return -1;
    }

    status = __make_folders(g_fridge.root);
    if (status) {
        VLOG_ERROR("fridge", "fridge_initialize: failed to create folders\n");
        fridge_cleanup();
        return -1;
    }

    // initialize the store inventory
    status = fridge_store_load(platform, architecture, &g_fridge.store);
    if (status) {
        VLOG_ERROR("fridge", "fridge_initialize: failed to load store inventory\n");
        fridge_cleanup();
        return -1;
    }
    return 0;
}

void fridge_cleanup(void)
{
    // free memory
    free(g_fridge.root);

    // reset context
    memset(&g_fridge, 0, sizeof(struct fridge_context));
}

int fridge_ensure_ingredient(struct fridge_ingredient* ingredient, const char** pathOut)
{
    struct fridge_inventory_pack* pack = NULL;
    int                           status;

    status = fridge_store_open(g_fridge.store);
    if (status) {
        return status;
    }

    status = fridge_store_ensure_ingredient(g_fridge.store, ingredient, &pack);
    if (status) {
        (void)fridge_store_close(g_fridge.store);
        return status;
    }
    if (pathOut != NULL) {
        *pathOut = strdup(inventory_pack_path(pack));
    }
    return fridge_store_close(g_fridge.store);
}
