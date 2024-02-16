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
#include <chef/platform.h>
#include <chef/kitchen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <vlog.h>

static void __print_help(void)
{
    printf("Usage: bake pack [options]\n");
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

    ingredient = malloc(sizeof(struct kitchen_ingredient));
    if (ingredient == NULL) {
        return -1;
    }
    memset(ingredient, 0, sizeof(struct kitchen_ingredient));

    ingredient->name = name;
    ingredient->path = path;

    list_add(kitchenIngredients, &ingredient->list_header);
}

static int __prep_ingredient_list(struct list* list, const char* platform, const char* arch, struct list* kitchenIngredients)
{
    struct list_item* item;
    int               status;
;
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

static int __prep_ingredients(struct recipe* recipe, const char* platform, const char* arch, struct kitchen_options* kitchenOptions)
{
    struct list_item* item;
    int               status;

    printf("preparing %i host ingredients\n", recipe->environment.host.ingredients.count);
    status = __prep_ingredient_list(
        &recipe->environment.host.ingredients,
        CHEF_PLATFORM_STR,
        CHEF_ARCHITECTURE_STR,
        &kitchenOptions->host_ingredients
    );
    if (status) {
        return status;
    }

    printf("preparing %i build ingredients\n", recipe->environment.build.ingredients.count);
    status = __prep_ingredient_list(
        &recipe->environment.build.ingredients,
        platform,
        arch,
        &kitchenOptions->build_ingredients
    );
    if (status) {
        return status;
    }

    printf("preparing %i runtime ingredients\n", recipe->environment.runtime.ingredients.count);
    status = __prep_ingredient_list(
        &recipe->environment.runtime.ingredients,
        platform,
        arch,
        &kitchenOptions->runtime_ingredients
    );
    if (status) {
        return status;
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

static int __parse_cc_switch(const char* value, char** platformOut, char** archOut)
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

static enum recipe_step_type __string_to_step_type(const char* type)
{
    if (strcmp(type, "generate") == 0) {
        return RECIPE_STEP_TYPE_GENERATE;
    } else if (strcmp(type, "build") == 0) {
        return RECIPE_STEP_TYPE_BUILD;
    } else if (strcmp(type, "script") == 0) {
        return RECIPE_STEP_TYPE_SCRIPT;
    } else {
        return RECIPE_STEP_TYPE_UNKNOWN;
    }
}

int run_main(int argc, char** argv, char** envp, struct recipe* recipe)
{
    struct oven_parameters ovenParams = { 0 };
    struct kitchen_options kitchenOptions = { 0 };
    struct kitchen         kitchen;
    char*                  name     = NULL;
    char*                  platform = CHEF_PLATFORM_STR;
    char*                  arch     = CHEF_ARCHITECTURE_STR;
    char*                  step     = "run";
    int                    debug    = 0;
    char                   tmp[128];
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
                status = __parse_cc_switch(argv[i], &platform, &arch);
                if (status) {
                    VLOG_ERROR("bake", "failed to parse cross-compile switch\n");
                    return status;
                }
            } else if (argv[i][0] != '-') {
                if (__is_step_name(argv[i])) {
                    step = argv[i];
                } else {
                    name = argv[i];
                }
            }
        }
    }

    if (name == NULL || recipe == NULL) {
        VLOG_ERROR("bake", "no recipe provided\n");
        __print_help();
        return -1;
    }

    // get the current working directory
    status = __get_cwd(&cwd);
    if (status) {
        return -1;
    }

    // get basename of recipe
    strbasename(name, tmp, sizeof(tmp));

    status = fridge_initialize(platform, arch);
    if (status != 0) {
        VLOG_ERROR("bake", "failed to initialize fridge\n");
        return -1;
    }
    atexit(fridge_cleanup);

    status = chefclient_initialize();
    if (status != 0) {
        VLOG_ERROR("bake", "failed to initialize chef client\n");
        return -1;
    }
    atexit(chefclient_cleanup);

    ovenParams.envp = (const char* const*)envp;
    ovenParams.target_platform = platform;
    ovenParams.target_architecture = arch;
    ovenParams.project_path = cwd;

    status = oven_initialize(&ovenParams);
    if (status) {
        VLOG_ERROR("bake", "failed to initialize oven: %s\n", strerror(errno));
        return -1;
    }
    atexit(oven_cleanup);

    status = __prep_ingredients(recipe, platform, arch, &kitchenOptions);
    if (status) {
        VLOG_ERROR("bake", "failed to fetch ingredients: %s\n", strerror(errno));
        return -1;
    }

    // prepare kitchen parameters, lists are already filled at this point
    kitchenOptions.name = &tmp[0];
    kitchenOptions.project_path = cwd;
    kitchenOptions.confined = recipe->environment.build.confinement;
    status = kitchen_setup(&kitchenOptions, &kitchen);
    if (status) {
        VLOG_ERROR("bake", "failed to setup kitchen: %s\n", strerror(errno));
        return -1;
    }

    status = kitchen_prepare_recipe(&kitchen, recipe, __string_to_step_type(step));
    if (status) {
        VLOG_ERROR("bake", "failed to reset steps: %s\n", strerror(errno));
        return -1;
    }

    status = kitchen_make_recipe(&kitchen, recipe);
    if (status) {
        VLOG_ERROR("bake", "failed to make recipes\n");
        if (debug) {
            __debug();
        }
        return -1;
    }

    if (strcmp(step, "run") == 0 || strcmp(step, "pack") == 0) {
        status = kitchen_make_packs(&kitchen, recipe);
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
