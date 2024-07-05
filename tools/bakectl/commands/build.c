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
    printf("Usage: bake run [options]\n");
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

static void __debug(void)
{
    // wait for any key and then return
    printf("press any key to continue\n");
    getchar();
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

    oven_recipe_options_construct(&options, part, toolchain);
    status = oven_recipe_start(&options);

    struct oven_build_options buildOptions;
    __initialize_build_options(&buildOptions, step);
    status = oven_build(&buildOptions);
    if (status) {
        VLOG_ERROR("bake", "failed to build target: %s\n", step->system);
        return status;
    }
    
    oven_recipe_end();
    
    return status;
}
