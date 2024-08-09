/**
 * Copyright 2024, Philip Meulengracht
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
#include <chef/list.h>
#include <chef/kitchen.h>
#include <chef/platform.h>
#include <chef/containerv.h>
#include <libpkgmgr.h>
#include <stdlib.h>
#include <string.h>
#include <vlog.h>
#include "private.h"

static void __safe_free(void** ptr) {
    free(*ptr);
    *ptr = NULL;
}

static void __safe_freev(void*** ptr) {
    void** ptrv = *ptr;
    if (ptr == NULL) {
        return;
    }
    
    for (int i = 0; ptrv[i] != NULL; i++) {
        free(ptrv[i]);
    }
    free(ptrv);
    *ptr = NULL;
}

void kitchen_destroy(struct kitchen* kitchen)
{
    if (kitchen->container != NULL) {
        if (containerv_destroy(kitchen->container)) {
            VLOG_ERROR("kitchen", "kitchen_destroy: failed to destroy container\n");
        }
        kitchen->container = NULL;
    }

    if (kitchen->pkg_manager != NULL) {
        kitchen->pkg_manager->destroy(kitchen->pkg_manager);
        kitchen->pkg_manager = NULL;
    }

    __safe_free((void**)&kitchen->recipe_path);
    __safe_free((void**)&kitchen->host_cwd);
    __safe_free((void**)&kitchen->target_platform);
    __safe_free((void**)&kitchen->target_architecture);
    __safe_freev((void***)&kitchen->base_environment);

    // external paths that point inside chroot
    // i.e paths valid outside chroot
    __safe_free((void**)&kitchen->host_chroot);
    __safe_free((void**)&kitchen->host_kitchen_project_root);
    __safe_free((void**)&kitchen->host_build_path);
    __safe_free((void**)&kitchen->host_build_ingredients_path);
    __safe_free((void**)&kitchen->host_build_toolchains_path);
    __safe_free((void**)&kitchen->host_project_path);
    __safe_free((void**)&kitchen->host_install_root);
    __safe_free((void**)&kitchen->host_install_path);

    // internal paths
    // i.e paths valid during chroot
    __safe_free((void**)&kitchen->project_root);
    __safe_free((void**)&kitchen->build_root);
    __safe_free((void**)&kitchen->build_ingredients_path);
    __safe_free((void**)&kitchen->build_toolchains_path);
    __safe_free((void**)&kitchen->install_root);
    __safe_free((void**)&kitchen->install_path);
    __safe_free((void**)&kitchen->bakectl_path);

    // do not free kitchen itself
}
