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
    printf("Usage: bakectl source [options]\n");
    printf("\n");
    printf("Options:\n");
    printf("  -s,  --step\n");
    printf("      If provided, sources only the provided part/step configuration\n");
    printf("  -h,  --help\n");
    printf("      Shows this help message\n");
}

static void __cleanup_systems(int sig)
{
    (void)sig;
    printf("termination requested, cleaning up\n"); // not safe
    _Exit(0);
}

static int __prepare_path(const char* root, const char* part, const char* relativePath)
{

}

static int __prepare_url(const char* root, const char* part)
{

}

static int __prepare_git(const char* root, const char* part)
{

}

static int __prepare_source(const char* part, struct recipe_part_source* source)
{
    const char* sourceRoot;

    switch (source->type) {
        case RECIPE_PART_SOURCE_TYPE_PATH: {
            return __prepare_path(sourceRoot, part, source->path.path);
        } break;
        case RECIPE_PART_SOURCE_TYPE_GIT: {
            return __prepare_git(sourceRoot, part);
        } break;
        case RECIPE_PART_SOURCE_TYPE_URL: {
            return __prepare_url(sourceRoot, part);
        } break;
    }

    errno = ENOSYS;
    return -1;
}

static int __source_part(struct recipe* recipe, const char* partName, const char* stepName, const char* platform)
{
    struct list_item* item;
    int               status;
    VLOG_DEBUG("bakectl", "__source_part()\n");

    recipe_cache_transaction_begin();
    list_foreach(&recipe->parts, item) {
        struct recipe_part* part = (struct recipe_part*)item;

        // find the correct recipe part
        if (partName != NULL && strcmp(part->name, partName)) {
            continue;
        }

        status = oven_recipe_start(&(struct oven_recipe_options) {
            .name = part->name
        });
        if (status) {
            break;
        }
        
        status = __prepare_source(part->name, &part->source);
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

int clean_main(int argc, char** argv, char** envp, struct bakectl_command_options* options)
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

    if (options->recipe == NULL) {
        fprintf(stderr, "bakectl: --recipe must be provided\n");
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

    status = __source_part(options->recipe, options->part, options->step, ovenOpts.target_platform);
    if (status) {
        fprintf(stderr, "bakectl: failed to clean step '%s/%s': %s\n", 
            options->part, options->step, strerror(errno));
    }
    
    oven_cleanup();

cleanup:
    __destroy_oven_options(&ovenOpts);
    return status;
}
