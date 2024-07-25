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
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <vlog.h>

static int __reset_steps(const char* part, struct list* steps, enum recipe_step_type stepType, const char* name);

static int __step_depends_on(struct list* dependencies, const char* step)
{
    struct list_item* item;
    VLOG_DEBUG("kitchen", "__step_depends_on(step=%s)\n", step);

    list_foreach(dependencies, item) {
        struct list_item_string* value = (struct list_item_string*)item;
        if (strcmp(value->value, step) == 0) {
            // OK this step depends on the step we are resetting
            // so reset this step too
            return 1;
        }
    }
    return 0;
}

static int __reset_depending_steps(const char* part, struct list* steps, const char* name)
{
    struct list_item* item;
    int               status;
    VLOG_DEBUG("kitchen", "__reset_depending_steps(name=%s)\n", name);

    list_foreach(steps, item) {
        struct recipe_step* recipeStep = (struct recipe_step*)item;

        // skip ourselves
        if (strcmp(recipeStep->name, name) != 0) {
            if (__step_depends_on(&recipeStep->depends, name)) {
                status = __reset_steps(part, steps, RECIPE_STEP_TYPE_UNKNOWN, recipeStep->name);
                if (status) {
                    VLOG_ERROR("bake", "failed to reset step %s\n", recipeStep->name);
                    return status;
                }
            }
        }
    }
    return 0;
}

static int __reset_steps(const char* part, struct list* steps, enum recipe_step_type stepType, const char* name)
{
    struct list_item* item;
    int               status;
    VLOG_DEBUG("kitchen", "__reset_steps(name=%s)\n", name);

    list_foreach(steps, item) {
        struct recipe_step* recipeStep = (struct recipe_step*)item;
        if ((stepType == RECIPE_STEP_TYPE_UNKNOWN) || (recipeStep->type == stepType) ||
            (name != NULL && strcmp(recipeStep->name, name) == 0)) {
            // this should be deleted
            status = recipe_cache_mark_step_incomplete(part, recipeStep->name);
            if (status) {
                VLOG_ERROR("bake", "failed to clear step %s\n", recipeStep->name);
                return status;
            }

            // clear dependencies
            status = __reset_depending_steps(part, steps, recipeStep->name);
        }
    }
    return 0;
}

int kitchen_recipe_clean(struct kitchen* kitchen, struct kitchen_recipe_clean_options* options)
{
    struct list_item* item;
    int               status;
    char              buffer[PATH_MAX];
    char*             partName;
    char*             stepName;
    VLOG_DEBUG("kitchen", "kitchen_recipe_clean()\n");

    status = recipe_parse_part_step(options->part_or_step, &partName, &stepName);
    if (status) {
        return status;
    }

    if (options->part_or_step != NULL) {
        snprintf(&buffer[0], sizeof(buffer), "clean --recipe %s --step %s", kitchen->recipe_path, options->part_or_step);
    } else {
        snprintf(&buffer[0], sizeof(buffer), "clean --recipe %s", kitchen->recipe_path);
    }

    status = containerv_spawn(
        kitchen->container,
        "bakectl",
        &(struct containerv_spawn_options) {
            .arguments = &buffer[0],
            .environment = (const char* const*)kitchen->base_environment,
            .flags = CV_SPAWN_WAIT
        },
        NULL
    );
    if (status) {
        VLOG_ERROR("bake", "failed to perform clean step of '%s'\n", kitchen->recipe->project.name);
        return status;
    }

    recipe_cache_transaction_begin();
    list_foreach(&kitchen->recipe->parts, item) {
        struct recipe_part* part = (struct recipe_part*)item;

        // Only if the part name is provided, check against it
        if (partName != NULL && strcmp(part->name, partName)) {
            continue;
        }
        
        status = __reset_steps(part->name, &part->steps, RECIPE_STEP_TYPE_UNKNOWN, stepName);
        if (status) {
            VLOG_ERROR("kitchen", "kitchen_recipe_clean: failed to clean recipe %s\n", part->name);
            break;
        }
    }
    recipe_cache_transaction_commit();
    return status;
}

int kitchen_purge(struct kitchen_purge_options* options)
{
    struct list         recipes;
    struct list_item*   i;
    int                 status;
    char                root[PATH_MAX] = { 0 };
    VLOG_DEBUG("kitchen", "kitchen_purge()\n");

    status = __get_kitchen_root(&root[0], sizeof(root) - 1, NULL);
    if (status) {
        VLOG_ERROR("kitchen", "kitchen_purge: failed to resolve root directory\n");
        return -1;
    }

    list_init(&recipes);
    status = platform_getfiles(&root[0], 0, &recipes);
    if (status) {
        // ignore this error, just means there is no cleanup to be done
        if (errno != ENOENT) {
            VLOG_ERROR("kitchen", "kitchen_purge: failed to get current recipes\n");
        }
        goto cleanup;
    }

    list_foreach (&recipes, i) {
        struct platform_file_entry* entry = (struct platform_file_entry*)i;
        status = platform_rmdir(entry->path);
        if (status) {
            VLOG_ERROR("kitchen", "kitchen_purge: failed to remove data for %s\n", entry->name);
            goto cleanup;
        }

        recipe_cache_transaction_begin();
        status = recipe_cache_clear_for(entry->name);
        if (status) {
            VLOG_ERROR("kitchen", "kitchen_purge: failed to clean cache for %s\n", entry->name);
            goto cleanup;
        }
        recipe_cache_transaction_commit();
    }

cleanup:
    platform_getfiles_destroy(&recipes);
    return 0;
}
