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
#include <libpkgmgr.h>
#include <stdlib.h>
#include "steps.h"
#include "user.h"
#include <vlog.h>


static void __initialize_generator_options(struct oven_generate_options* options, struct recipe_step* step)
{
    options->name           = step->name;
    options->profile        = NULL;
    options->system         = step->system;
    options->system_options = &step->options;
    options->arguments      = &step->arguments;
    options->environment    = &step->env_keypairs;
}

static void __initialize_build_options(struct oven_build_options* options, struct recipe_step* step)
{
    options->name           = step->name;
    options->profile        = NULL;
    options->system         = step->system;
    options->system_options = &step->options;
    options->arguments      = &step->arguments;
    options->environment    = &step->env_keypairs;
}

static void __initialize_script_options(struct oven_script_options* options, struct recipe_step* step)
{
    options->name   = step->name;
    options->script = step->script;
}

static int __make_recipe_steps(struct kitchen* kitchen, const char* part, struct list* steps)
{
    struct list_item* item;
    int               status;
    VLOG_DEBUG("kitchen", "__make_recipe_steps(part=%s)\n", part);
    
    list_foreach(steps, item) {
        struct recipe_step* step = (struct recipe_step*)item;
        if (recipe_cache_is_step_complete(part, step->name)) {
            VLOG_TRACE("bake", "nothing to be done for step %s/%s\n", part, step->name);
            continue;
        }

        VLOG_TRACE("bake", "preparing step '%s'\n", step->system);
        if (kitchen->pkg_manager != NULL) {
            kitchen->pkg_manager->add_overrides(kitchen->pkg_manager, &step->env_keypairs);
        }

        VLOG_TRACE("bake", "executing step '%s'\n", step->system);
        if (step->type == RECIPE_STEP_TYPE_GENERATE) {
            struct oven_generate_options genOptions;
            __initialize_generator_options(&genOptions, step);
            status = oven_configure(&genOptions);
            if (status) {
                VLOG_ERROR("bake", "failed to configure target: %s\n", step->system);
                return status;
            }
        } else if (step->type == RECIPE_STEP_TYPE_BUILD) {
            struct oven_build_options buildOptions;
            __initialize_build_options(&buildOptions, step);
            status = oven_build(&buildOptions);
            if (status) {
                VLOG_ERROR("bake", "failed to build target: %s\n", step->system);
                return status;
            }
        } else if (step->type == RECIPE_STEP_TYPE_SCRIPT) {
            struct oven_script_options scriptOptions;
            __initialize_script_options(&scriptOptions, step);
            status = oven_script(&scriptOptions);
            if (status) {
                VLOG_ERROR("bake", "failed to execute script\n");
                return status;
            }
        }

        status = recipe_cache_mark_step_complete(part, step->name);
        if (status) {
            VLOG_ERROR("bake", "failed to mark step %s/%s complete\n", part, step->name);
            return status;
        }
    }
    
    return 0;
}

int kitchen_recipe_make(struct kitchen* kitchen, struct recipe* recipe)
{
    struct oven_recipe_options options;
    struct list_item*          item;
    struct kitchen_user        user;
    int                        status;
    VLOG_DEBUG("kitchen", "kitchen_recipe_make()\n");

    if (kitchen_user_new(&user)) {
        VLOG_ERROR("kitchen", "kitchen_recipe_make: failed to get current user\n");
        return -1;
    }

    status = kitchen_cooking_start(kitchen);
    if (status) {
        VLOG_ERROR("kitchen", "kitchen_recipe_make: failed to start cooking: %i\n", status);
        kitchen_user_delete(&user);
        return status;
    }

    if (kitchen_user_drop_privs(&user)) {
        VLOG_ERROR("kitchen", "kitchen_recipe_make: failed to drop privileges\n");
        goto cleanup;
    }

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

        status = __make_recipe_steps(kitchen, part->name, &part->steps);
        oven_recipe_end();

        if (status) {
            VLOG_ERROR("bake", "kitchen_recipe_make: failed to build recipe %s\n", part->name);
            break;
        }
    }

    status = kitchen_user_regain_privs(&user);
    if (status) {
        VLOG_ERROR("kitchen", "kitchen_recipe_make: failed to re-escalate privileges\n");
    }

cleanup:
    if (kitchen_cooking_end(kitchen)) {
        VLOG_ERROR("kitchen", "kitchen_recipe_make: failed to end cooking\n");
    }
    kitchen_user_delete(&user);
    return status;
}

