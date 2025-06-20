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
    printf("Usage: bakectl clean [options]\n");
    printf("\n");
    printf("Options:\n");
    printf("  -s,  --step\n");
    printf("      If provided, cleans only the provided part/step configuration\n");
    printf("  -p,  --purge\n");
    printf("      Purges all build configurations for the recipe\n");
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

static void __initialize_clean_options(struct oven_clean_options* options, struct recipe_step* step)
{
    options->name           = step->name;
    options->profile        = NULL;
    options->system         = step->system;
    options->system_options = &step->options;
    options->arguments      = &step->arguments;
    options->environment    = &step->env_keypairs;
}

static int __clean_step(const char* partName, struct list* steps, const char* stepName)
{
    struct list_item* item;
    int               status;
    char              buffer[512];
    VLOG_DEBUG("bakectl", "__clean_step(part=%s, step=%s)\n", partName, stepName);
    
    list_foreach(steps, item) {
        struct oven_clean_options cleanOptions;
        struct recipe_step* step = (struct recipe_step*)item;

        // find the correct recipe step part
        if (stepName != NULL && strcmp(step->name, stepName)) {
            continue;
        }

        __initialize_clean_options(&cleanOptions, step);
        status = oven_clean(&cleanOptions);
        if (status) {
            VLOG_ERROR("bakectl", "failed to clean target: %s\n", step->system);
            return status;
        }

        // done if a specific step was provided
        if (stepName != NULL) {
            break;
        }
    }
    return 0;
}

static int __clean_part(struct recipe* recipe, const char* partName, const char* stepName, const char* platform)
{
    struct list_item* item;
    int               status;
    VLOG_DEBUG("bakectl", "__clean_part()\n");

    list_foreach(&recipe->parts, item) {
        struct recipe_part* part = (struct recipe_part*)item;
        char*               toolchain = NULL;

        // find the correct recipe part
        if (partName != NULL && strcmp(part->name, partName)) {
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

        status = __clean_step(part->name, &part->steps, stepName);
        oven_recipe_end();

        if (status) {
            VLOG_ERROR("bakectl", "__clean_part: failed to build recipe %s\n", part->name);
            break;
        }

        // done if a specific part was provided
        if (partName != NULL) {
            break;
        }
    }

    return status;
}

static int __recreate_dir(const char* path)
{
    int status;

    status = platform_rmdir(path);
    if (status) {
        if (errno != ENOENT) {
            VLOG_ERROR("kitchen", "__recreate_dir: failed to remove directory: %s\n", strerror(errno));
            return -1;
        }
    }

    status = platform_mkdir(path);
    if (status) {
        VLOG_ERROR("kitchen", "__recreate_dir: failed to create directory: %s\n", strerror(errno));
        return -1;
    }
    return 0;
}

int clean_main(int argc, char** argv, struct __bakelib_context* context, struct bakectl_command_options* options)
{
    struct oven_initialize_options ovenOpts = { 0 };
    int                            status;
    int                            purge = 0;

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

    status = __initialize_oven_options(&ovenOpts, context);
    if (status) {
        fprintf(stderr, "bakectl: failed to allocate memory for options\n");
        goto cleanup;
    }

    status = oven_initialize(&ovenOpts);
    if (status) {
        fprintf(stderr, "bakectl: failed to initialize oven: %s\n", strerror(errno));
        goto cleanup;
    }

    if (purge) {
        // eh clean entire build tree
        status = __recreate_dir(ovenOpts.paths.build_root);
        if (status) {
            fprintf(stderr, "bakectl: failed to clean path '%s': %s\n", 
                ovenOpts.paths.build_root, strerror(errno));
        }
    } else {
        status = __clean_part(context->recipe, options->part, options->step, ovenOpts.target_platform);
        if (status) {
            fprintf(stderr, "bakectl: failed to clean step '%s/%s': %s\n", 
                options->part, options->step, strerror(errno));
        }
    }
    
    oven_cleanup();

cleanup:
    __destroy_oven_options(&ovenOpts);
    return status;
}
