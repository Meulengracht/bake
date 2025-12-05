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

#include <errno.h>
#include <chef/config.h>
#include <chef/client.h>
#include <chef/cvd.h>
#include <chef/dirs.h>
#include <chef/list.h>
#include <chef/platform.h>
#include <chef/recipe.h>
#include <chef/store-default.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <vlog.h>

#include "commands.h"

static struct __bake_build_context* g_context = NULL;

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

static int __ensure_toolchains(struct list* platforms)
{
    struct list_item* item;
    VLOG_DEBUG("bake", "__prep_toolchains()\n");

    list_foreach(platforms, item) {
        struct recipe_platform* platform = (struct recipe_platform*)item;
        int                     status;
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

        status = store_ensure_package(&(struct store_package) {
            .name = name,
            .channel = channel,
            .arch = CHEF_ARCHITECTURE_STR,
            .platform = CHEF_PLATFORM_STR
        }, NULL);
        if (status) {
            free(name);
            free(channel);
            free(version);
            VLOG_ERROR("bake", "failed to fetch ingredient %s\n", name);
            return status;
        }
    }
    return 0;
}

static int __ensure_ingredient_list(struct list* list, const char* platform, const char* arch)
{
    struct list_item* item;
    int               status;
    VLOG_DEBUG("bake", "__prep_ingredient_list(platform=%s, arch=%s)\n", platform, arch);

    list_foreach(list, item) {
        struct recipe_ingredient* ingredient = (struct recipe_ingredient*)item;

        status = store_ensure_package(&(struct store_package) {
            .name = ingredient->name,
            .channel = ingredient->channel,
            .arch = arch,
            .platform = platform
        }, NULL);
        if (status) {
            VLOG_ERROR("bake", "failed to fetch ingredient %s\n", ingredient->name);
            return status;
        }
    }
    return 0;
}

static int __ensure_ingredients(struct recipe* recipe, const char* platform, const char* arch)
{
    struct list_item* item;
    int               status;

    if (recipe->platforms.count > 0) {
        VLOG_TRACE("bake", "preparing %i platforms\n", recipe->platforms.count);
        status = __ensure_toolchains(&recipe->platforms);
        if (status) {
            return status;
        }
    }

    if (recipe->environment.host.ingredients.count > 0) {
        VLOG_TRACE("bake", "preparing %i host ingredients\n", recipe->environment.host.ingredients.count);
        status = __ensure_ingredient_list(
            &recipe->environment.host.ingredients,
            CHEF_PLATFORM_STR,
            CHEF_ARCHITECTURE_STR
        );
        if (status) {
            return status;
        }
    }

    if (recipe->environment.build.ingredients.count > 0) {
        VLOG_TRACE("bake", "preparing %i build ingredients\n", recipe->environment.build.ingredients.count);
        status = __ensure_ingredient_list(
            &recipe->environment.build.ingredients,
            platform,
            arch
        );
        if (status) {
            return status;
        }
    }

    if (recipe->environment.runtime.ingredients.count > 0) {
        VLOG_TRACE("bake", "preparing %i runtime ingredients\n", recipe->environment.runtime.ingredients.count);
        status = __ensure_ingredient_list(
            &recipe->environment.runtime.ingredients,
            platform,
            arch
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
    build_context_destroy(g_context);

    // cleanup logging
    vlog_cleanup();

    // Do a quick exit, which is recommended to do in signal handlers
    // and use the signal as the exit code
    _Exit(-sig);
}

static char* __add_build_log(void)
{
    char* path;
    FILE* stream = chef_dirs_contemporary_file("bake-build", "log", &path);
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
    struct build_cache*        cache = NULL;
    struct chef_config*        config;
    struct chef_config_address cvdAddress;
    int                        status;
    char*                      logPath;
    char*                      header;
    char*                      footer;
    const char*                arch;
    struct vlog_step           step_prepare;
    struct vlog_step           step_source;
    struct vlog_step           step_build;
    struct vlog_step           step_pack;

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

    config = chef_config_load(chef_dirs_config());
    if (config == NULL) {
        VLOG_ERROR("remote", "remote_client_create: failed to load configuration\n");
        return -1;
    }
    chef_config_cvd_address(config, &cvdAddress);

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

    // TODO: make chefclient instanced, move to store
    status = chefclient_initialize();
    if (status != 0) {
        VLOG_ERROR("bake", "failed to initialize chef client\n");
        return -1;
    }
    atexit(chefclient_cleanup);

    status = store_initialize(&(struct store_parameters) {
        .platform = options->platform,
        .architecture = arch,
        .backend = g_store_default_backend
    });
    if (status != 0) {
        VLOG_ERROR("bake", "failed to initialize store\n");
        return -1;
    }
    atexit(store_cleanup);

    // setup the build log box
    vlog_start(stdout, header, footer, 6);

    // 0+1 are informational
    vlog_content_set_index(0);
    vlog_content_set_prefix("pkg-env");

    vlog_content_set_index(1);
    vlog_content_set_prefix("");

    // initialize pipeline steps
    vlog_step_init(&step_prepare, 2, "prepare");
    vlog_step_init(&step_source,  3, "source");
    vlog_step_init(&step_build,   4, "build");
    vlog_step_init(&step_pack,    5, "pack");

    // use prepare for initial information
    vlog_step_begin(&step_prepare);

    // we want the recipe cache in this case for regular builds
    status = build_cache_create(options->recipe, options->cwd, &cache);
    if (status) {
        VLOG_ERROR("kitchen", "failed to initialize build cache\n");
        return -1;
    }

    // debug target information
    VLOG_DEBUG("bake", "platform=%s, architecture=%s\n", options->platform, arch);
    g_context = build_context_create(&(struct __bake_build_options) {
        .cwd = options->cwd,
        .envp = (const char* const*)envp,
        .recipe = options->recipe,
        .recipe_path = options->recipe_path,
        .build_cache = cache,
        .target_platform = options->platform,
        .target_architecture = arch,
        .cvd_address = &cvdAddress
    });
    if (g_context == NULL) {
        VLOG_ERROR("bake", "failed to initialize build context: %s\n", strerror(errno));
        return -1;
    }

    status = __ensure_ingredients(options->recipe, options->platform, arch);
    if (status) {
        VLOG_ERROR("bake", "failed to fetch ingredients: %s\n", strerror(errno));
        vlog_step_fail(&step_prepare);
        goto cleanup;
    }

    status = bake_build_setup(g_context);
    if (status) {
        VLOG_ERROR("bake", "failed to setup build environment: %s\n", strerror(errno));
        vlog_step_fail(&step_prepare);
        goto cleanup;
    }

    vlog_step_end(&step_prepare, 1);

    vlog_step_begin(&step_source);
    status = build_step_source(g_context);
    if (status) {
        vlog_step_fail(&step_source);
        goto cleanup;
    }
    vlog_step_end(&step_source, 1);

    vlog_step_begin(&step_build);
    status = build_step_make(g_context);
    if (status) {
        vlog_step_fail(&step_build);
        goto cleanup;
    }
    vlog_step_end(&step_build, 1);

    vlog_step_begin(&step_pack);
    status = build_step_pack(g_context);
    vlog_step_end(&step_pack, status == 0);

cleanup:
    vlog_refresh(stdout);
    vlog_end();
    build_context_destroy(g_context);
    return status;
}
