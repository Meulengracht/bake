/**
 * Copyright, Philip Meulengracht
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
#include <chef/dirs.h>
#include <chef/platform.h>
#include <chef/recipe.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <vlog.h>

#include "commands.h"
#include "build-helpers/build.h"

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
    struct __bake_build_context* context;
    struct build_cache*  cache = NULL;
    int                  purge = 0;
    char*                partOrStep = NULL;
    int                  status;
    const char*          arch;

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
        return bake_purge_kitchens();
    }

    if (options->recipe == NULL) {
        fprintf(stderr, "bake: no recipe provided\n");
        __print_help();
        return -1;
    }

    // get the architecture from the list
    arch = ((struct list_item_string*)options->architectures.head)->value;

    // we want the recipe cache in this case for regular cleans
    status = build_cache_create(options->recipe, options->cwd, &cache);
    if (status) {
        VLOG_ERROR("kitchen", "failed to initialize recipe cache\n");
        return -1;
    }

    context = build_context_create(&(struct __bake_build_options) {
        .cwd = options->cwd,
        .envp = (const char* const*)envp,
        .recipe = options->recipe,
        .recipe_path = options->recipe_path,
        .build_cache = cache,
        .target_platform = options->platform,
        .target_architecture = arch,
        .cvd_address = NULL
    });
    if (status) {
        VLOG_ERROR("bake", "failed to initialize kitchen: %s\n", strerror(errno));
        return -1;
    }

    // ignore CTRL-C request once cleanup starts
    signal(SIGINT, SIG_IGN);

    status = bake_step_clean(context,
        &(struct __build_clean_options) {
            .part_or_step = partOrStep
        }
    );

    build_context_destroy(context);
    return status;
}
