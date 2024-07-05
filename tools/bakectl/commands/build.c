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

#include <chef/client.h>
#include <errno.h>
#include <liboven.h>
#include <chef/list.h>
#include <chef/platform.h>
#include <chef/kitchen.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <vlog.h>

#include "commands.h"

static void __print_help(void)
{
    printf("Usage: bakectl run [options]\n");
    printf("\n");
    printf("Options:\n");
    printf("  -cc, --cross-compile\n");
    printf("      Cross-compile for another platform or/and architecture. This switch\n");
    printf("      can be used with two different formats, either just like\n");
    printf("      --cross-compile=arch or --cross-compile=platform/arch\n");
    printf("  -h,  --help\n");
    printf("      Shows this help message\n");
}

static void __cleanup_systems(int sig)
{
    (void)sig;
    printf("termination requested, cleaning up\n"); // not safe
    exit(0); // not safe, manually clean up systems and call _Exit()
}

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

static int __build_step(struct kitchen* kitchen, const char* part, struct list* steps, const char* step)
{
    struct list_item* item;
    int               status;
    char              buffer[512];
    VLOG_DEBUG("kitchen", "__make_recipe_steps(part=%s, step=%s)\n", part, step);
    
    list_foreach(steps, item) {
        struct recipe_step* step = (struct recipe_step*)item;
        if (recipe_cache_is_step_complete(part, step->name)) {
            VLOG_TRACE("bake", "nothing to be done for step '%s/%s'\n", part, step->name);
            continue;
        }

        snprintf(&buffer[0], "--recipe %s --step %s/%s", kitchen->recipe_path, part, step->system);

        VLOG_TRACE("bake", "executing step '%s/%s'\n", part, step->system);
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
            VLOG_ERROR("bake", "failed to execute step '%s/%s'\n", part, step->system);
            return status;
        }

        status = recipe_cache_mark_step_complete(part, step->name);
        if (status) {
            VLOG_ERROR("bake", "failed to mark step %s/%s complete\n", part, step->name);
            return status;
        }
    }
    
    return 0;
}

static int __build_part(struct kitchen* kitchen, struct recipe* recipe)
{
    struct oven_recipe_options options;
    struct list_item*          item;
    int                        status;
    VLOG_DEBUG("kitchen", "kitchen_recipe_make()\n");

    recipe_cache_transaction_begin();
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

cleanup:
    recipe_cache_transaction_commit();
    return status;
}


int build_main(int argc, char** argv, char** envp, struct bakectl_command_options* options)
{
    struct kitchen_init_options  initOptions = { 0 };
    struct kitchen_setup_options setupOptions = { 0 };
    struct oven_recipe_options   options;
    struct list_item*            item;
    int                          status;

    // catch CTRL-C
    signal(SIGINT, __cleanup_systems);

    // handle individual help command
    if (argc > 1) {
        for (int i = 1; i < argc; i++) {
            if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
                __print_help();
                return 0;
            }
        }
    }

    if (options->recipe == NULL) {
        fprintf(stderr, "bake: no recipe provided\n");
        __print_help();
        return -1;
    }


    snprintf(&buff[0], sizeof(buff), "/chef/build/%s/%s",
        options->target_platform, options->target_architecture
    );
    kitchen->build_root = strdup(&buff[0]);

    snprintf(&buff[0], sizeof(buff), "/chef/ingredients/%s/%s",
        options->target_platform, options->target_architecture
    );
    kitchen->build_ingredients_path = strdup(&buff[0]);

    snprintf(&buff[0], sizeof(buff), "/chef/install/%s/%s",
        options->target_platform, options->target_architecture
    );
    kitchen->install_path = strdup(&buff[0]);

    status = oven_initialize(&(struct oven_initialize_options){
        .envp = (const char* const*)envp,
        .target_architecture = getenv("CHEF_TARGET_ARCH"),
        .target_platform = getenv("CHEF_TARGET_PLATFORM"),
        .paths = {
            .project_root = "/chef/project",
            .build_root = kitchen->build_root,
            .install_root = kitchen->install_path,
            .toolchains_root = "/chef/toolchains",
            .build_ingredients_root = kitchen->build_ingredients_path
        }
    });
    if (status) {
        VLOG_ERROR("kitchen", "kitchen_setup: failed to initialize oven: %s\n", strerror(errno));
        goto cleanup;
    }

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
    
    // cleanup oven
    oven_cleanup();

    return status;
}
