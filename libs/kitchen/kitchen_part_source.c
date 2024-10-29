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
#include <stdlib.h>
#include <vlog.h>

int kitchen_recipe_source(struct kitchen* kitchen)
{
    struct list_item* item;
    int               status;
    char              buffer[512];
    VLOG_DEBUG("kitchen", "kitchen_recipe_source()\n");

    __KITCHEN_IF_CACHE(kitchen, recipe_cache_transaction_begin(kitchen->recipe_cache));
    list_foreach(&kitchen->recipe->parts, item) {
        struct recipe_part* part = (struct recipe_part*)item;
        
        if (kitchen->recipe_cache != NULL && 
                recipe_cache_is_part_sourced(kitchen->recipe_cache, part->name)) {
            VLOG_TRACE("kitchen", "part '%s' already sourced\n", part->name);
            continue;
        }

        snprintf(&buffer[0], sizeof(buffer),
            "source --project %s --recipe %s --step %s", 
            kitchen->project_root, kitchen->recipe_path, part->name
        );

        VLOG_TRACE("kitchen", "sourcing part '%s'\n", part->name);
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
            VLOG_ERROR("kitchen", "failed to source part '%s'\n", part->name);
            return status;
        }

        if (kitchen->recipe_cache != NULL) {
            status = recipe_cache_mark_part_sourced(kitchen->recipe_cache, part->name);
            if (status) {
                VLOG_ERROR("kitchen", "failed to mark part '%s' sourced\n", part->name);
                return status;
            }
        }
    }
    __KITCHEN_IF_CACHE(kitchen, recipe_cache_transaction_commit(kitchen->recipe_cache));
    return status;
}

