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
 * - api-keys
 * - pack deletion
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
            .source = INGREDIENT_SOURCE_TYPE_REPO, // for now
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
            .source = ingredient->source,
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

static int __is_step_name(const char* name)
{
    return strcmp(name, "run") == 0 ||
           strcmp(name, "generate") == 0 ||
           strcmp(name, "build") == 0 ||
           strcmp(name, "script") == 0 ||
           strcmp(name, "pack") == 0;
}

static int __parse_cc_switch(const char* value, const char** platformOut, const char** archOut)
{
    // value is either of two forms
    // platform/arch
    // arch
    char* separator;
    char* equal;

    if (value == NULL) {
        errno = EINVAL;
        return -1;
    }

    equal = strchr(value, '=');
    if (equal == NULL) {
        VLOG_ERROR("bake", "invalid format of %s (must be -cc=... or --cross-compile=...)\n", value);
        errno = EINVAL;
        return -1;
    }

    // skip the '='
    equal++;

    separator = strchr(equal, '/');
    if (separator) {
        *platformOut = strndup(equal, separator - equal);
        *archOut     = strdup(separator + 1);
    } else {
        *platformOut = strdup(CHEF_PLATFORM_STR);
        *archOut     = strdup(equal);
    }
    return 0;
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

int run_main(int argc, char** argv, char** envp, struct recipe* recipe)
{
    struct kitchen_setup_options kitchenOptions = { 0 };
    struct kitchen         kitchen;
    const char*            platform = NULL;
    const char*            arch     = NULL;
    char*                  step     = "run";
    int                    debug    = 0;
    char*                  cwd;
    int                    status;

    // catch CTRL-C
    signal(SIGINT, __cleanup_systems);

    // handle individual help command
    if (argc > 1) {
        for (int i = 1; i < argc; i++) {
            if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
                __print_help();
                return 0;
            } else if (!strcmp(argv[i], "-d") || !strcmp(argv[i], "--debug")) {
                debug = 1;
            } else if (!strncmp(argv[i], "-cc", 3) || !strncmp(argv[i], "--cross-compile", 15)) {
                // THIS ALLOCS MEMORY, WE NEED TO HANDLE THIS
                status = __parse_cc_switch(argv[i], &platform, &arch);
                if (status) {
                    VLOG_ERROR("bake", "invalid format: %s\n", argv[i]);
                    return status;
                }
            } else if (argv[i][0] != '-') {
                if (__is_step_name(argv[i])) {
                    step = argv[i];
                }
            }
        }
    }

    if (recipe == NULL) {
        VLOG_ERROR("bake", "no recipe provided\n");
        __print_help();
        return -1;
    }

    status = recipe_validate_target(recipe, &platform, &arch);
    if (status) {
        return -1;
    }

    VLOG_TRACE("bake", "target platform: %s\n", platform);
    VLOG_TRACE("bake", "target architecture: %s\n", arch);

    // get the current working directory
    status = __get_cwd(&cwd);
    if (status) {
        return -1;
    }

    // TODO: make chefclient instanced, move to fridge
    status = chefclient_initialize();
    if (status != 0) {
        VLOG_ERROR("bake", "failed to initialize chef client\n");
        return -1;
    }
    atexit(chefclient_cleanup);

    status = fridge_initialize(platform, arch);
    if (status != 0) {
        VLOG_ERROR("bake", "failed to initialize fridge\n");
        return -1;
    }
    atexit(fridge_cleanup);

    status = __prep_ingredients(recipe, platform, arch, &kitchenOptions);
    if (status) {
        VLOG_ERROR("bake", "failed to fetch ingredients: %s\n", strerror(errno));
        return -1;
    }

    // setup linux options
    kitchenOptions.packages = &recipe->environment.host.packages;

    // setup kitchen hooks
    kitchenOptions.setup_hook.bash = recipe->environment.hooks.bash;
    kitchenOptions.setup_hook.powershell = recipe->environment.hooks.powershell;

    // prepare kitchen parameters, lists are already filled at this point
    kitchenOptions.name = recipe->project.name;
    kitchenOptions.project_path = cwd;
    kitchenOptions.pkg_environment = NULL;
    kitchenOptions.confined = recipe->environment.build.confinement;
    kitchenOptions.envp = (const char* const*)envp;
    kitchenOptions.target_platform = platform;
    kitchenOptions.target_architecture = arch;
    status = kitchen_setup(&kitchenOptions, &kitchen);
    if (status) {
        VLOG_ERROR("bake", "failed to setup kitchen: %s\n", strerror(errno));
        return -1;
    }

    status = kitchen_recipe_prepare(&kitchen, recipe, recipe_step_type_from_string(step));
    if (status) {
        VLOG_ERROR("bake", "failed to reset steps: %s\n", strerror(errno));
        return -1;
    }

    status = kitchen_recipe_make(&kitchen, recipe);
    if (status) {
        VLOG_ERROR("bake", "failed to make recipes\n");
        if (debug) {
            __debug();
        }
        return -1;
    }

    if (strcmp(step, "run") == 0 || strcmp(step, "pack") == 0) {
        status = kitchen_recipe_pack(&kitchen, recipe);
        if (status) {
            VLOG_ERROR("bake", "failed to construct packs\n");
            if (debug) {
                __debug();
            }
            return -1;
        }
    }
    return status;
}
