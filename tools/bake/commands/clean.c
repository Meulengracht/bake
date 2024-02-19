/**
 * Copyright 2022, Philip Meulengracht
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
 * Package System TODOs:
 * - autotools backend
 * - reuse zstd context for improved performance
 * Application System TODOs:
 * - app commands
 * - served system
 */

#include <errno.h>
#include <chef/platform.h>
#include <chef/recipe.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

static void __print_help(void)
{
    printf("Usage: bake clean [options]\n");
    printf("\n");
    printf("Options:\n");
    printf("  -h, --help\n");
    printf("      Shows this help message\n");
}

static void __cleanup_systems(int sig)
{
    (void)sig;
    printf("bake: termination requested, cleaning up\n");
    exit(0);
}

int clean_main(int argc, char** argv, char** envp, struct recipe* recipe)
{
    struct oven_parameters ovenParams;
    int                    status;
    char*                  name = NULL;

    // catch CTRL-C
    signal(SIGINT, __cleanup_systems);

    // handle individual help command
    if (argc > 2) {
        for (int i = 2; i < argc; i++) {
            if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
                __print_help();
                return 0;
            } else if (argv[i][0] != '-') {
                name = argv[i];
            }
        }
    }

    if (name == NULL) {
        // should not happen
        fprintf(stderr, "bake: missing recipe name\n");
        return -1;
    }

    if (recipe == NULL) {
        fprintf(stderr, "bake: no recipe provided\n");
        __print_help();
        return -1;
    }

    // TODO: use kitchen_recipe_clean

    return status;
}
