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

#include <chef/list.h>
#include <chef/recipe.h>
#include <chef/platform.h>
#include <errno.h>
#include <stdlib.h>
#include <vlog.h>

#include "build.h"

static int __make_recipe_steps(struct __bake_build_context* bctx, const char* part, struct list* steps)
{
    struct list_item* item;
    int               status;
    char              buffer[PATH_MAX];
    unsigned int      pid;
    VLOG_DEBUG("kitchen", "__make_recipe_steps(part=%s)\n", part);

    list_foreach(steps, item) {
        struct recipe_step* step = (struct recipe_step*)item;

        snprintf(&buffer[0], sizeof(buffer),
            "%s build --recipe %s --step %s/%s",
            bctx->bakectl_path, bctx->recipe_path, part, step->name
        );

        VLOG_TRACE("kitchen", "executing step '%s/%s'\n", part, step->name);
        status = bake_client_spawn(
            bctx,
            &buffer[0],
            CHEF_SPAWN_OPTIONS_WAIT,
            &pid
        );
        if (status) {
            VLOG_ERROR("kitchen", "failed to execute step '%s/%s'\n", part, step->name);
            return status;
        }
    }
    
    return 0;
}

int build_step_make(struct __bake_build_context* bctx)
{
    struct list_item* item;
    int               status;
    VLOG_DEBUG("kitchen", "kitchen_recipe_make()\n");

    if (bctx->cvd_client == NULL) {
        errno = ENOTSUP;
        return -1;
    }

    list_foreach(&bctx->recipe->parts, item) {
        struct recipe_part* part = (struct recipe_part*)item;

        status = __make_recipe_steps(bctx, part->name, &part->steps);
        if (status) {
            VLOG_ERROR("kitchen", "kitchen_recipe_make: failed to build recipe %s\n", part->name);
            break;
        }
    }
    return status;
}

