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
    printf("Usage: bake clean [options|type]\n");
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

static int __ask_yes_no_question(const char* question)
{
    char answer[3] = { 0 };
    printf("%s (default=no) [Y/n] ", question);
    if (fgets(answer, sizeof(answer), stdin) == NULL) {
        return 0;
    }
    return answer[0] == 'Y';
}

static int __is_clean_type(const char* type)
{
    if (strcmp(type, "all") == 0) {
        return 1;
    } else if (strcmp(type, "build") == 0) {
        return 0;
    }
    return 0;
}

int clean_main(int argc, char** argv, char** envp, struct recipe* recipe)
{
    int   purge = 0;
    char* type = "all";

    // handle individual help command
    if (argc > 2) {
        for (int i = 2; i < argc; i++) {
            if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
                __print_help();
                return 0;
            } else if (!strcmp(argv[i], "--purge")) {
                purge = 1;
            } else if (argv[i][0] != '-' && __is_clean_type(argv[i])) {
                type = argv[i];
            }
        }
    }

    // initialize the recipe cache
    if (recipe_cache_initialize(recipe)) {
        fprintf(stderr, "bake: failed to initialize recipe cache\n");
        return -1;
    }

    // if purge was set, then clean the entire kitchen
    if (purge) {
        if (!__ask_yes_no_question("this will clean up ALL bake recipes in the kitchen area, proceed?")) {
            return 0;
        }
        printf("proceeding with cleanup\n");

        // ignore CTRL-C request once cleanup starts
        signal(SIGINT, SIG_IGN);

        // purge all kitchen recipes
        return kitchen_purge(NULL);
    }

    if (recipe == NULL) {
        fprintf(stderr, "bake: no recipe provided\n");
        __print_help();
        return -1;
    }

    // ignore CTRL-C request once cleanup starts
    signal(SIGINT, SIG_IGN);

    if (strcmp(type, "build") == 0) {
        return kitchen_recipe_clean(recipe);
    } else if (strcmp(type, "all") == 0) {
        return kitchen_recipe_purge(recipe, NULL);
    }
    
    fprintf(stderr, "bake: %s is not recognized as an action\n", type);
    return -1;
}
