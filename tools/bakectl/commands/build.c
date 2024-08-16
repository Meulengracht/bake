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
#include <liboven.h>
#include <chef/list.h>
#include <chef/platform.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <vlog.h>

#include "commands.h"

static void __print_help(void)
{
    printf("Usage: bakectl build [options]\n");
    printf("\n");
    printf("Options:\n");
    printf("  -s,  --step\n");
    printf("      If provided, builds only the provided part/step configuration\n");
    printf("  -h,  --help\n");
    printf("      Shows this help message\n");
}

static void __cleanup_systems(int sig)
{
    (void)sig;
    printf("termination requested, cleaning up\n"); // not safe
    _Exit(0);
}

static char* __resolve_toolchain(struct recipe* recipe, const char* toolchain, const char* platform)
{
    if (strcmp(toolchain, "platform") == 0) {
        const char* fullChain = recipe_find_platform_toolchain(recipe, platform);
        char*       name;
        char*       channel;
        char*       version;
        if (fullChain == NULL) {
            return NULL;
        }
        if (recipe_parse_platform_toolchain(fullChain, &name, &channel, &version)) {
            return NULL;
        }
        free(channel);
        free(version);
        return name;
    }
    return platform_strdup(toolchain);
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

static int __build_step(const char* partName, struct list* steps, const char* stepName)
{
    struct list_item* item;
    int               status;
    char              buffer[512];
    VLOG_DEBUG("bakectl", "__build_step(part=%s, step=%s)\n", partName, stepName);
    
    list_foreach(steps, item) {
        struct recipe_step* step = (struct recipe_step*)item;

        // find the correct recipe step part
        if (strcmp(step->name, stepName)) {
            continue;
        }

        if (step->type == RECIPE_STEP_TYPE_GENERATE) {
            struct oven_generate_options genOptions;
            __initialize_generator_options(&genOptions, step);
            status = oven_configure(&genOptions);
            if (status) {
                VLOG_ERROR("bakectl", "failed to configure target: %s\n", step->system);
                return status;
            }
        } else if (step->type == RECIPE_STEP_TYPE_BUILD) {
            struct oven_build_options buildOptions;
            __initialize_build_options(&buildOptions, step);
            status = oven_build(&buildOptions);
            if (status) {
                VLOG_ERROR("bakectl", "failed to build target: %s\n", step->system);
                return status;
            }
        } else if (step->type == RECIPE_STEP_TYPE_SCRIPT) {
            struct oven_script_options scriptOptions;
            __initialize_script_options(&scriptOptions, step);
            status = oven_script(&scriptOptions);
            if (status) {
                VLOG_ERROR("bakectl", "failed to execute script\n");
                return status;
            }
        } else {
            VLOG_ERROR("bakectl", "unknown step type: %i\n", step->type);
            return -1;
        }

        // done if a specific step was provided
        if (stepName != NULL) {
            break;
        }
    }
    return 0;
}

static int __build_part(struct recipe* recipe, const char* partName, const char* stepName, const char* platform)
{
    struct list_item* item;
    int               status;
    VLOG_DEBUG("bakectl", "__build_part(part=%s, step=%s, platform=%s)\n", partName, stepName, platform);

    recipe_cache_transaction_begin();
    list_foreach(&recipe->parts, item) {
        struct recipe_part* part = (struct recipe_part*)item;
        char*               toolchain = NULL;

        // find the correct recipe part
        if (strcmp(part->name, partName)) {
            continue;
        }

        if (part->toolchain != NULL) {
            toolchain = __resolve_toolchain(recipe, part->toolchain, platform);
            if (toolchain == NULL) {
                VLOG_ERROR("bakectl", "part %s was marked for platform toolchain, but no matching toolchain specified for platform %s\n", part->name, platform);
                return -1;
            }
        }

        status = oven_recipe_start(&(struct oven_recipe_options) {
            .name = part->name,
            .toolchain = toolchain
        });
        free(toolchain);
        if (status) {
            break;
        }

        status = __build_step(part->name, &part->steps, stepName);
        oven_recipe_end();

        if (status) {
            VLOG_ERROR("bakectl", "__build_part: failed to build recipe %s\n", part->name);
            break;
        }

        // done if a specific part was provided
        if (partName != NULL) {
            break;
        }
    }

    return status;
}

int build_main(int argc, char** argv, char** envp, struct bakectl_command_options* options)
{
    struct oven_initialize_options ovenOpts = { 0 };
    int                            status;

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
        fprintf(stderr, "bakectl: --recipe must be provided\n");
        return -1;
    }

    if (options->part == NULL || options->step == NULL) {
        fprintf(stderr, "bakectl: --step must be provided and have a valid format of '<part>/<step>'\n");
        return -1;
    }

    status = __initialize_oven_options(&ovenOpts, envp);
    if (status) {
        fprintf(stderr, "bakectl: failed to allocate memory for options\n");
        goto cleanup;
    }

    status = oven_initialize(&ovenOpts);
    if (status) {
        fprintf(stderr, "bakectl: failed to initialize oven: %s\n", strerror(errno));
        goto cleanup;
    }
    
    status = __build_part(options->recipe, options->part, options->step, ovenOpts.target_platform);
    if (status) {
        fprintf(stderr, "bakectl: failed to build: %s\n", strerror(errno));
    }
    oven_cleanup();

cleanup:
    __destroy_oven_options(&ovenOpts);
    return status;
}
