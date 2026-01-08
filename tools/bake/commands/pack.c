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
#include <chef/cvd.h>
#include <chef/dirs.h>
#include <chef/pack.h>
#include <chef/platform.h>
#include <chef/recipe.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <vlog.h>

#include "commands.h"

static void __print_help(void)
{
    printf("Usage: bake pack [options] <dir>\n");
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

int pack_main(int argc, char** argv, char** envp, struct bake_command_options* options)
{
    struct __bake_build_context* bctx;
    struct build_cache*          cache = NULL;
    const char*                  arch;
    int                          status;

    // handle individual help command
    if (argc > 2) {
        for (int i = 2; i < argc; i++) {
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

    // get the architecture from the list
    arch = ((struct list_item_string*)options->architectures.head)->value;

    // we need to load the recipe cache
    status = build_cache_create(options->recipe, options->cwd, &cache);
    if (status) {
        VLOG_ERROR("kitchen", "failed to initialize build cache\n");
        return -1;
    }

    // then construct the options
    VLOG_DEBUG("bake", "platform=%s, architecture=%s\n", options->platform, arch);
    bctx = build_context_create(&(struct __bake_build_options) {
        .cwd = options->cwd,
        .envp = (const char* const*)envp,
        .recipe = options->recipe,
        .recipe_path = options->recipe_path,
        .build_cache = cache,
        .target_platform = options->platform,
        .target_architecture = arch,
        .cvd_address = NULL
    });
    if (bctx == NULL) {
        VLOG_ERROR("bake", "failed to initialize build context: %s\n", strerror(errno));
        return -1;
    }

    status = build_step_pack(bctx);
    if (status) {
        VLOG_ERROR("bake", "failed to pack recipe: %s\n", strerror(errno));
        goto cleanup;
    }

cleanup:
    build_context_destroy(bctx);
    return status;
}
