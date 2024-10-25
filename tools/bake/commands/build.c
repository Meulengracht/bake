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
 * Package System TODOs:
 * - api-keys
 * - pack deletion
 */
#define _GNU_SOURCE

#include <chef/client.h>
#include <errno.h>
#include <chef/dirs.h>
#include <chef/list.h>
#include <chef/platform.h>
#include <chef/kitchen.h>
#include <ctype.h>
#include <libfridge.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <vlog.h>

#include "commands.h"

static struct kitchen g_kitchen = { 0 };

static void __print_help(void)
{
    printf("Usage: bake build [options]\n");
    printf("\n");
    printf("Options:\n");
    printf("  -cc, --cross-compile\n");
    printf("      Cross-compile for another platform or/and architecture. This switch\n");
    printf("      can be used with two different formats, either just like\n");
    printf("      --cross-compile=arch or --cross-compile=platform/arch\n");
    printf("  -h,  --help\n");
    printf("      Shows this help message\n");
}

static int __add_kitchen_ingredient(const char* name, const char* path, struct list* kitchenIngredients)
{
    struct kitchen_ingredient* ingredient;
    VLOG_DEBUG("bake", "__add_kitchen_ingredient(name=%s, path=%s)\n", name, path);

    ingredient = malloc(sizeof(struct kitchen_ingredient));
    if (ingredient == NULL) {
        return -1;
    }
    memset(ingredient, 0, sizeof(struct kitchen_ingredient));

    ingredient->name = name;
    ingredient->path = path;

    list_add(kitchenIngredients, &ingredient->list_header);
    return 0;
}

static int __prep_toolchains(struct list* platforms, struct list* kitchenIngredients)
{
    struct list_item* item;
    VLOG_DEBUG("bake", "__prep_toolchains()\n");

    list_foreach(platforms, item) {
        struct recipe_platform* platform = (struct recipe_platform*)item;
        int                     status;
        const char*             path;
        char*                   name;
        char*                   channel;
        char*                   version;
        if (platform->toolchain == NULL) {
            continue;
        }
        
        status = recipe_parse_platform_toolchain(platform->toolchain, &name, &channel, &version);
        if (status) {
            VLOG_ERROR("bake", "failed to parse toolchain %s for platform %s", platform->toolchain, platform->name);
            return status;
        }

        status = fridge_ensure_ingredient(&(struct fridge_ingredient) {
            .name = name,
            .channel = channel,
            .version = version,
            .arch = CHEF_ARCHITECTURE_STR,
            .platform = CHEF_PLATFORM_STR
        }, &path);
        if (status) {
            free(name);
            free(channel);
            free(version);
            VLOG_ERROR("bake", "failed to fetch ingredient %s\n", name);
            return status;
        }
        
        status = __add_kitchen_ingredient(name, path, kitchenIngredients);
        if (status) {
            free(name);
            free(channel);
            free(version);
            VLOG_ERROR("bake", "failed to mark ingredient %s\n", name);
            return status;
        }
    }
    return 0;
}

static int __prep_ingredient_list(struct list* list, const char* platform, const char* arch, struct list* kitchenIngredients)
{
    struct list_item* item;
    int               status;
    VLOG_DEBUG("bake", "__prep_ingredient_list(platform=%s, arch=%s)\n", platform, arch);

    list_foreach(list, item) {
        struct recipe_ingredient* ingredient = (struct recipe_ingredient*)item;
        const char*               path = NULL;

        status = fridge_ensure_ingredient(&(struct fridge_ingredient) {
            .name = ingredient->name,
            .channel = ingredient->channel,
            .version = ingredient->version,
            .arch = arch,
            .platform = platform
        }, &path);
        if (status) {
            VLOG_ERROR("bake", "failed to fetch ingredient %s\n", ingredient->name);
            return status;
        }
        
        status = __add_kitchen_ingredient(ingredient->name, path, kitchenIngredients);
        if (status) {
            VLOG_ERROR("bake", "failed to mark ingredient %s\n", ingredient->name);
            return status;
        }
    }
    return 0;
}

static int __prep_ingredients(struct recipe* recipe, const char* platform, const char* arch, struct kitchen_setup_options* kitchenOptions)
{
    struct list_item* item;
    int               status;

    if (recipe->platforms.count > 0) {
        VLOG_TRACE("bake", "preparing %i platforms\n", recipe->platforms.count);
        status = __prep_toolchains(
            &recipe->platforms,
            &kitchenOptions->host_ingredients
        );
        if (status) {
            return status;
        }
    }

    if (recipe->environment.host.ingredients.count > 0) {
        VLOG_TRACE("bake", "preparing %i host ingredients\n", recipe->environment.host.ingredients.count);
        status = __prep_ingredient_list(
            &recipe->environment.host.ingredients,
            CHEF_PLATFORM_STR,
            CHEF_ARCHITECTURE_STR,
            &kitchenOptions->host_ingredients
        );
        if (status) {
            return status;
        }
    }

    if (recipe->environment.build.ingredients.count > 0) {
        VLOG_TRACE("bake", "preparing %i build ingredients\n", recipe->environment.build.ingredients.count);
        status = __prep_ingredient_list(
            &recipe->environment.build.ingredients,
            platform,
            arch,
            &kitchenOptions->build_ingredients
        );
        if (status) {
            return status;
        }
    }

    if (recipe->environment.runtime.ingredients.count > 0) {
        VLOG_TRACE("bake", "preparing %i runtime ingredients\n", recipe->environment.runtime.ingredients.count);
        status = __prep_ingredient_list(
            &recipe->environment.runtime.ingredients,
            platform,
            arch,
            &kitchenOptions->runtime_ingredients
        );
        if (status) {
            return status;
        }
    }
    return 0;
}

static void __cleanup_systems(int sig)
{
    // printing as a part of a signal handler is not safe
    // but we live dangerously
    vlog_content_set_status(VLOG_CONTENT_STATUS_FAILED);
    vlog_end();

    // cleanup the kitchen, this will take out most of the systems
    // setup as a part of all this.
    kitchen_destroy(&g_kitchen);

    // cleanup logging
    vlog_cleanup();

    // Do a quick exit, which is recommended to do in signal handlers
    // and use the signal as the exit code
    _Exit(-sig);
}

static char* __add_build_log(void)
{
    char* path;
    FILE* stream = chef_dirs_contemporary_file("bake-build", ".log", &path);
    if (stream == NULL) {
        return NULL;
    }

    vlog_add_output(stream, 1);
    vlog_set_output_level(stream, VLOG_LEVEL_DEBUG);
    return path;
}

static char* __format_header(const char* name, const char* platform, const char* arch)
{
    char tmp[512];
    snprintf(&tmp[0], sizeof(tmp), "%s (%s, %s)", name, platform, arch);
    return platform_strdup(&tmp[0]);
}

static char* __format_footer(const char* logPath)
{
    char tmp[PATH_MAX];
    snprintf(&tmp[0], sizeof(tmp), "build log: %s", logPath);
    return platform_strdup(&tmp[0]);
}

int run_main(int argc, char** argv, char** envp, struct bake_command_options* options)
{
    struct kitchen_setup_options setupOptions = { 0 };
    int                          status;
    char*                        logPath;
    char*                        header;
    char*                        footer;
    const char*                  arch;

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

    if (options->architectures.count > 1) {
        fprintf(stderr, "bake: multiple architectures are not supported\n");
        return -1;
    }

    logPath = __add_build_log();
    if (logPath == NULL) {
        fprintf(stderr, "bake: failed to open build log\n");
        return -1;
    }

    // get the architecture from the list
    arch = ((struct list_item_string*)options->architectures.head)->value;

    header = __format_header(options->recipe->project.name, options->platform, arch);
    footer = __format_footer(logPath);
    free(logPath);
    if (footer == NULL) {
        fprintf(stderr, "bake: failed to allocate memory for build footer\n");
        return -1;
    }

    // TODO: make chefclient instanced, move to fridge
    status = chefclient_initialize();
    if (status != 0) {
        VLOG_ERROR("bake", "failed to initialize chef client\n");
        return -1;
    }
    atexit(chefclient_cleanup);

    status = fridge_initialize(options->platform, arch);
    if (status != 0) {
        VLOG_ERROR("bake", "failed to initialize fridge\n");
        return -1;
    }
    atexit(fridge_cleanup);

    // setup the build log box
    vlog_start(stdout, header, footer, 6);

    // 0+1 are informational
    vlog_content_set_index(0);
    vlog_content_set_prefix("pkg-env");

    vlog_content_set_index(1);
    vlog_content_set_prefix("");

    vlog_content_set_index(2);
    vlog_content_set_prefix("prepare");
    vlog_content_set_status(VLOG_CONTENT_STATUS_WAITING);

    vlog_content_set_index(3);
    vlog_content_set_prefix("source");
    vlog_content_set_status(VLOG_CONTENT_STATUS_WAITING);

    vlog_content_set_index(4);
    vlog_content_set_prefix("build");
    vlog_content_set_status(VLOG_CONTENT_STATUS_WAITING);

    vlog_content_set_index(5);
    vlog_content_set_prefix("pack");
    vlog_content_set_status(VLOG_CONTENT_STATUS_WAITING);

    // use 2 for initial information (prepare)
    vlog_content_set_index(2);
    vlog_content_set_status(VLOG_CONTENT_STATUS_WORKING);

    // debug target information
    VLOG_DEBUG("bake", "platform=%s, architecture=%s\n", options->platform, arch);
    status = kitchen_initialize(&(struct kitchen_init_options) {
        .recipe = options->recipe,
        .recipe_path = options->recipe_path,
        .envp = (const char* const*)envp,
        .project_path = options->cwd,
        .pkg_environment = NULL,
        .target_platform = options->platform,
        .target_architecture = arch,
    }, &g_kitchen);
    if (status) {
        VLOG_ERROR("bake", "failed to initialize kitchen: %s\n", strerror(errno));
        return -1;
    }

    status = __prep_ingredients(options->recipe, options->platform, arch, &setupOptions);
    if (status) {
        VLOG_ERROR("bake", "failed to fetch ingredients: %s\n", strerror(errno));
        goto cleanup;
    }

    // setup linux options
    setupOptions.packages = &options->recipe->environment.host.packages;

    // setup kitchen hooks
    setupOptions.setup_hook.bash = options->recipe->environment.hooks.bash;
    setupOptions.setup_hook.powershell = options->recipe->environment.hooks.powershell;
    status = kitchen_setup(&g_kitchen, &setupOptions);
    if (status) {
        vlog_content_set_status(VLOG_CONTENT_STATUS_FAILED);
        goto cleanup;
    }

    // update status of logging
    vlog_content_set_status(VLOG_CONTENT_STATUS_DONE);

    vlog_content_set_index(3);
    vlog_content_set_status(VLOG_CONTENT_STATUS_WORKING);

    status = kitchen_recipe_source(&g_kitchen);
    if (status) {
        vlog_content_set_status(VLOG_CONTENT_STATUS_FAILED);
        goto cleanup;
    }

    // update status of logging
    vlog_content_set_status(VLOG_CONTENT_STATUS_DONE);

    vlog_content_set_index(4);
    vlog_content_set_status(VLOG_CONTENT_STATUS_WORKING);

    status = kitchen_recipe_make(&g_kitchen);
    if (status) {
        vlog_content_set_status(VLOG_CONTENT_STATUS_FAILED);
        goto cleanup;
    }

    // update status of logging
    vlog_content_set_status(VLOG_CONTENT_STATUS_DONE);

    vlog_content_set_index(5);
    vlog_content_set_status(VLOG_CONTENT_STATUS_WORKING);

    status = kitchen_recipe_pack(&g_kitchen);
    if (status) {
        vlog_content_set_status(VLOG_CONTENT_STATUS_FAILED);
    }

    // update status of logging
    vlog_content_set_status(VLOG_CONTENT_STATUS_DONE);

cleanup:
    vlog_refresh(stdout);
    vlog_end();
    kitchen_destroy(&g_kitchen);
    return status;
}
