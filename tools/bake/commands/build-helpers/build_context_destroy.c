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

#include <chef/environment.h>
#include <chef/platform.h>
#include <stdlib.h>
#include <vlog.h>

#include "build.h"

// build cache is not owned by the build context
void build_context_destroy(struct __bake_build_context* bctx)
{
    int status;

    // destroy container
    status = bake_client_destroy_container(bctx);
    if (status) {
        VLOG_ERROR("bake", "build_context_destroy: failed to destroy the build container\n");
    }

    // cleanup client
    if (bctx->cvd_client != NULL) { 
        gracht_client_shutdown(bctx->cvd_client);
    }

    // cleanup allocated environment
    environment_destroy((char**)bctx->base_environment);

    // free resources
    free((void*)bctx->recipe_path);
    free((void*)bctx->host_cwd);
    free((void*)bctx->bakectl_path);
    free((void*)bctx->rootfs_path);
    free((void*)bctx->install_path);
    free((void*)bctx->build_ingredients_path);
    free((void*)bctx->target_architecture);
    free((void*)bctx->target_platform);
    free((void*)bctx->cvd_id);
}
