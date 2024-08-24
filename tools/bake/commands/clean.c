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

#include "commands.h"

static struct kitchen g_kitchen = { 0 };

static void __print_help(void)
{
    printf("Usage: bake clean [options|type]\n");
    printf("\n");
    printf("Options:\n");
    printf("  --purge\n");
    printf("      cleans all active recipes in the kitchen area\n");
    printf("  -cc, --cross-compile\n");
    printf("      Cross-compile for another platform or/and architecture. This switch\n");
    printf("      can be used with two different formats, either just like\n");
    printf("      --cross-compile=arch or --cross-compile=platform/arch\n");
    printf("  -h, --help\n");
    printf("      Shows this help message\n");
}

static void __cleanup_systems(int sig)
{
    // printing as a part of a signal handler is not safe
    // but we live dangerously
    printf("termination requested, cleaning up\n");

    // cleanup the kitchen, this will take out most of the systems
    // setup as a part of all this.
    kitchen_destroy(&g_kitchen);

    // Do a quick exit, which is recommended to do in signal handlers
    // and use the signal as the exit code
    _Exit(-sig);
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

int clean_main(int argc, char** argv, char** envp, struct bake_command_options* options)
{
    int   purge = 0;
    char* partOrStep = NULL;
    int   status;

    // handle individual help command
    if (argc > 2) {
        for (int i = 2; i < argc; i++) {
            if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
                __print_help();
                return 0;
            } else if (!strcmp(argv[i], "--purge")) {
                purge = 1;
            } else if (argv[i][0] != '-') {
                partOrStep = argv[i];
            }
        }
    }

    // if purge was set, then clean the entire kitchen
    if (purge) {
        if (!__ask_yes_no_question("this will clean up ALL bake recipes in the kitchen area, proceed?")) {
            return 0;
        }

        // ignore CTRL-C request once cleanup starts
        signal(SIGINT, SIG_IGN);

        // purge all kitchen recipes
        return kitchen_purge(NULL);
    }

    if (options->recipe == NULL) {
        fprintf(stderr, "bake: no recipe provided\n");
        __print_help();
        return -1;
    }

    status = kitchen_initialize(&(struct kitchen_init_options) {
        .recipe = options->recipe,
        .project_path = options->cwd,
        .pkg_environment = NULL,
        .target_platform = options->platform,
        .target_architecture = options->architecture
    }, &g_kitchen);
    if (status) {
        VLOG_ERROR("bake", "failed to initialize kitchen: %s\n", strerror(errno));
        return -1;
    }

    // ignore CTRL-C request once cleanup starts
    signal(SIGINT, SIG_IGN);

    status = kitchen_recipe_clean(&g_kitchen,
        &(struct kitchen_recipe_clean_options) {
            .part_or_step = partOrStep
        }
    );

    kitchen_destroy(&g_kitchen);
    return status;
}
