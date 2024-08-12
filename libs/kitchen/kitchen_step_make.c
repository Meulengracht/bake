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
#include <chef/kitchen.h>
#include <chef/containerv.h>
#include <libpkgmgr.h>
#include <stdlib.h>
#include <vlog.h>

static int __make_recipe_steps(struct kitchen* kitchen, const char* part, struct list* steps)
{
    struct list_item* item;
    int               status;
    char              buffer[512];
    VLOG_DEBUG("kitchen", "__make_recipe_steps(part=%s)\n", part);
    
    list_foreach(steps, item) {
        struct recipe_step* step = (struct recipe_step*)item;
        if (recipe_cache_is_step_complete(part, step->name)) {
            VLOG_TRACE("kitchen", "nothing to be done for step '%s/%s'\n", part, step->name);
            continue;
        }

        snprintf(&buffer[0], sizeof(buffer),
            "build -v --project %s --recipe %s --step %s/%s", 
            kitchen->project_root, kitchen->recipe_path, part, step->name
        );

        VLOG_TRACE("kitchen", "executing step '%s/%s'\n", part, step->name);
        status = containerv_spawn(
            kitchen->container,
            kitchen->bakectl_path,
            &(struct containerv_spawn_options) {
                .arguments = &buffer[0],
                .environment = (const char* const*)kitchen->base_environment,
                .flags = CV_SPAWN_WAIT,
            },
            NULL
        );
        if (status) {
            VLOG_ERROR("kitchen", "failed to execute step '%s/%s'\n", part, step->name);
            return status;
        }

        status = recipe_cache_mark_step_complete(part, step->name);
        if (status) {
            VLOG_ERROR("kitchen", "failed to mark step '%s/%s' complete\n", part, step->name);
            return status;
        }
    }
    
    return 0;
}

int kitchen_recipe_make(struct kitchen* kitchen, struct recipe* recipe)
{
    struct list_item* item;
    int               status;
    VLOG_DEBUG("kitchen", "kitchen_recipe_make()\n");

    recipe_cache_transaction_begin();
    list_foreach(&recipe->parts, item) {
        struct recipe_part* part = (struct recipe_part*)item;

        status = __make_recipe_steps(kitchen, part->name, &part->steps);
        if (status) {
            VLOG_ERROR("kitchen", "kitchen_recipe_make: failed to build recipe %s\n", part->name);
            break;
        }
    }

    recipe_cache_transaction_commit();
    return status;
}

