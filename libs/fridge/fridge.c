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
#include <chef/ingredient.h>
#include "inventory.h"
#include <chef/fridge.h>
#include <string.h>
#include "store.h"
#include <vlog.h>

struct progress_context {
    struct ingredient* ingredient;
    int                disabled;

    int files;
    int directories;
    int symlinks;
};

struct fridge_context {
    struct fridge_store* store;
};

static struct fridge_context g_fridge = { 0 };

int fridge_initialize(struct fridge_parameters* parameters)
{
    int status;

    if (parameters->platform == NULL || parameters->architecture == NULL) {
        VLOG_ERROR("fridge", "fridge_initialize: platform and architecture must be specified\n");
        return -1;
    }

    // initialize the store inventory
    status = fridge_store_load(parameters->platform, parameters->architecture, &parameters->backend, &g_fridge.store);
    if (status) {
        VLOG_ERROR("fridge", "fridge_initialize: failed to load store inventory\n");
        fridge_cleanup();
        return -1;
    }
    return 0;
}

void fridge_cleanup(void)
{
    // reset context
    memset(&g_fridge, 0, sizeof(struct fridge_context));
}

int fridge_ensure_ingredient(struct fridge_ingredient* ingredient)
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
    return fridge_store_close(g_fridge.store);
}

int fridge_ingredient_path(struct fridge_ingredient* ingredient, const char** pathOut)
{
    struct fridge_inventory_pack* pack = NULL;
    int                           status;

    status = fridge_store_open(g_fridge.store);
    if (status) {
        return status;
    }

    status = fridge_store_find_ingredient(g_fridge.store, ingredient, &pack);
    *pathOut = platform_strdup(inventory_pack_path(pack));
    return fridge_store_close(g_fridge.store);
}
