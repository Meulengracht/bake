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
#include <stdlib.h>
#include <string.h>
#include "steps.h"
#include "user.h"
#include <vlog.h>

static int __reset_steps(struct list* steps, enum recipe_step_type stepType, const char* name);

static int __step_depends_on(struct list* dependencies, const char* step)
{
    struct list_item* item;
    VLOG_DEBUG("kitchen", "__step_depends_on(step=%s)\n", step);

    list_foreach(dependencies, item) {
        struct oven_value_item* value = (struct oven_value_item*)item;
        if (strcmp(value->value, step) == 0) {
            // OK this step depends on the step we are resetting
            // so reset this step too
            return 1;
        }
    }
    return 0;
}

static int __reset_depending_steps(struct list* steps, const char* name)
{
    struct list_item* item;
    int               status;
    VLOG_DEBUG("kitchen", "__reset_depending_steps(name=%s)\n", name);

    list_foreach(steps, item) {
        struct recipe_step* recipeStep = (struct recipe_step*)item;

        // skip ourselves
        if (strcmp(recipeStep->name, name) != 0) {
            if (__step_depends_on(&recipeStep->depends, name)) {
                status = __reset_steps(steps, RECIPE_STEP_TYPE_UNKNOWN, recipeStep->name);
                if (status) {
                    VLOG_ERROR("bake", "failed to reset step %s\n", recipeStep->name);
                    return status;
                }
            }
        }
    }
    return 0;
}

static int __reset_steps(struct list* steps, enum recipe_step_type stepType, const char* name)
{
    struct list_item* item;
    int               status;
    VLOG_DEBUG("kitchen", "__reset_steps(name=%s)\n", name);

    list_foreach(steps, item) {
        struct recipe_step* recipeStep = (struct recipe_step*)item;
        if ((stepType == RECIPE_STEP_TYPE_UNKNOWN) || (recipeStep->type == stepType) ||
            (name && strcmp(recipeStep->name, name) == 0)) {
            // this should be deleted
            status = oven_clear_recipe_checkpoint(recipeStep->name);
            if (status) {
                VLOG_ERROR("bake", "failed to clear checkpoint %s\n", recipeStep->name);
                return status;
            }

            // clear dependencies
            status = __reset_depending_steps(steps, recipeStep->name);
        }
    }
    return 0;
}

int kitchen_recipe_clean(struct kitchen* kitchen)
{
    struct oven_recipe_options options;
    struct list_item*          item;
    int                        status;
    VLOG_DEBUG("kitchen", "kitchen_recipe_clean()\n");

    list_foreach(&recipe->parts, item) {
        struct recipe_part* part = (struct recipe_part*)item;
        char*               toolchain = NULL;

        if (part->toolchain != NULL) {
            toolchain = kitchen_toolchain_resolve(recipe, part->toolchain, kitchen->target_platform);
            if (toolchain == NULL) {
                VLOG_ERROR("kitchen", "part %s was marked for platform toolchain, but no matching toolchain specified for platform %s\n", part->name, kitchen->target_platform);
                return -1;
            }
        }

        oven_recipe_options_construct(&options, part, toolchain);
        status = oven_recipe_start(&options);
        free(toolchain);
        if (status) {
            break;
        }

        status = __reset_steps(&part->steps, RECIPE_STEP_TYPE_UNKNOWN, NULL);
        oven_recipe_end();

        if (status) {
            VLOG_ERROR("kitchen", "kitchen_recipe_clean: failed to build recipe %s\n", part->name);
            break;
        }
    }
    return status;
}
