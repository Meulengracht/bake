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

#include <chef/cvd.h>
#include <chef/list.h>
#include <chef/platform.h>
#include <chef/recipe.h>
#include <errno.h>
#include <stdlib.h>
#include <vlog.h>

int build_step_source(struct __bake_build_context* bctx)
{
    struct list_item* item;
    int               status;
    char              buffer[PATH_MAX];
    unsigned int      pid;
    VLOG_DEBUG("bake", "build_step_source()\n");

    if (bctx->cvd_client == NULL) {
        errno = ENOTSUP;
        return -1;
    }

    list_foreach(&bctx->recipe->parts, item) {
        struct recipe_part* part = (struct recipe_part*)item;
        
        snprintf(&buffer[0], sizeof(buffer),
            "%s source --recipe %s --step %s",
            bctx->bakectl_path, bctx->recipe_path, part->name
        );

        VLOG_TRACE("bake", "sourcing part '%s'\n", part->name);
        status = bake_client_spawn(
            bctx,
            &buffer[0],
            CHEF_SPAWN_OPTIONS_WAIT,
            &pid
        );
        if (status) {
            VLOG_ERROR("bake", "failed to source part '%s'\n", part->name);
            break;
        }
    }
    return status;
}

