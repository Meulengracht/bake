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
#include <chef/kitchen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <vlog.h>

static void __print_help(void)
{
    printf("Usage: bake clean [options]\n");
    printf("\n");
    printf("Options:\n");
    printf("  --purge\n");
    printf("      cleans all active recipes in the kitchen area\n");
    printf("  -h, --help\n");
    printf("      Shows this help message\n");
}

static void __cleanup_systems(int sig)
{
    (void)sig;
    printf("bake: termination requested, cleaning up\n");
    exit(0);
}

static int __get_cwd(char** bufferOut)
{
    char*  cwd;
    int    status;

    cwd = malloc(4096);
    if (cwd == NULL) {
        return -1;
    }

    status = platform_getcwd(cwd, 4096);
    if (status) {
        // buffer was too small
        VLOG_ERROR("oven", "could not get current working directory, buffer too small?\n");
        free(cwd);
        return -1;
    }
    *bufferOut = cwd;
    return 0;
}

int clean_main(int argc, char** argv, char** envp, struct recipe* recipe)
{
    int   status;
    int   purge = 0;
    char* name = NULL;
    char* cwd;
    char  tmp[128];

    // catch CTRL-C
    signal(SIGINT, __cleanup_systems);

    // handle individual help command
    if (argc > 2) {
        for (int i = 2; i < argc; i++) {
            if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
                __print_help();
                return 0;
            } else if (!strcmp(argv[i], "--purge")) {
                purge = 1;
            } else if (argv[i][0] != '-') {
                name = argv[i];
            }
        }
    }

    // get the current working directory
    status = __get_cwd(&cwd);
    if (status) {
        VLOG_ERROR("bake", "failed to determine project directory\n");
        return -1;
    }

    // if purge was set, then clean the entire kitchen
    if (purge) {
        return kitchen_purge(&(struct kitchen_purge_options) {
            .project_path = cwd
        });
    }

    if (name == NULL || recipe == NULL) {
        VLOG_ERROR("bake", "no recipe provided\n");
        __print_help();
        return -1;
    }

    // get basename of recipe
    strbasename(name, tmp, sizeof(tmp));

    return kitchen_recipe_clean(recipe, 
        &(struct kitchen_clean_options) {
            .name = &tmp[0],
            .project_path = cwd
        }
    );
}
