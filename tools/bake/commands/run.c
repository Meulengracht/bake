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
#include <libplatform.h>
#include <recipe.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

static void __print_help(void)
{
    printf("Usage: bake pack [options]\n");
    printf("\n");
    printf("Options:\n");
    printf("  -a, --arch\n");
    printf("      The target architecture to build for (default: host-arch)\n");
    printf("  -n, --name\n");
    printf("      The name for the produced container image (default: recipe-name)\n");
    printf("  -h, --help\n");
    printf("      Shows this help message\n");
}

static void __initialize_generator_options(struct oven_generate_options* options, struct recipe_step* step)
{
    options->name           = step->name;
    options->profile        = NULL;
    options->system         = step->system;
    options->system_options = &step->options;
    options->arguments      = &step->arguments;
    options->environment    = &step->env_keypairs;
}

static void __initialize_build_options(struct oven_build_options* options, struct recipe_step* step)
{
    options->name           = step->name;
    options->profile        = NULL;
    options->system         = step->system;
    options->system_options = &step->options;
    options->arguments      = &step->arguments;
    options->environment    = &step->env_keypairs;
}

static void __initialize_script_options(struct oven_script_options* options, struct recipe_step* step)
{
    options->name   = step->name;
    options->script = step->script;
}

static void __initialize_pack_options(
    struct oven_pack_options* options, 
    struct recipe*            recipe,
    struct recipe_pack*       pack)
{
    options->name        = pack->name;
    options->type        = pack->type;
    options->summary     = recipe->project.summary;
    options->description = recipe->project.description;
    options->icon        = recipe->project.icon;
    options->version     = recipe->project.version;
    options->license     = recipe->project.license;
    options->eula        = recipe->project.eula;
    options->author      = recipe->project.author;
    options->email       = recipe->project.email;
    options->url         = recipe->project.url;
    options->filters     = &pack->filters;
    options->commands    = &pack->commands;
}

static int __prep_ingredients(struct recipe* recipe)
{
    struct list_item* item;
    int               status;

    if (recipe->ingredients.count == 0) {
        return 0;
    }

    printf("bake: preparing %i ingredients\n", recipe->ingredients.count);
    list_foreach(&recipe->ingredients, item) {
        struct recipe_ingredient* ingredient = (struct recipe_ingredient*)item;
        status = fridge_store_ingredient(&ingredient->ingredient);
        if (status != 0) {
            fprintf(stderr, "bake: failed to fetch ingredient %s\n", ingredient->ingredient.name);
            return status;
        }
    }

    list_foreach(&recipe->ingredients, item) {
        struct recipe_ingredient* ingredient = (struct recipe_ingredient*)item;
        status = fridge_use_ingredient(&ingredient->ingredient);
        if (status != 0) {
            fprintf(stderr, "bake: failed to fetch ingredient %s\n", ingredient->ingredient.name);
            return status;
        }
    }
    return 0;
}

static int __make_recipe_steps(struct list* steps)
{
    struct list_item* item;
    int               status;
    
    list_foreach(steps, item) {
        struct recipe_step* step = (struct recipe_step*)item;
        printf("bake: executing step '%s'\n", step->system);

        if (step->type == RECIPE_STEP_TYPE_GENERATE) {
            struct oven_generate_options genOptions;
            __initialize_generator_options(&genOptions, step);
            status = oven_configure(&genOptions);
            if (status) {
                fprintf(stderr, "bake: failed to configure target: %s\n", step->system);
                return status;
            }
        } else if (step->type == RECIPE_STEP_TYPE_BUILD) {
            struct oven_build_options buildOptions;
            __initialize_build_options(&buildOptions, step);
            status = oven_build(&buildOptions);
            if (status) {
                fprintf(stderr, "bake: failed to build target: %s\n", step->system);
                return status;
            }
        } else if (step->type == RECIPE_STEP_TYPE_SCRIPT) {
            struct oven_script_options scriptOptions;
            __initialize_script_options(&scriptOptions, step);
            status = oven_script(&scriptOptions);
            if (status) {
                fprintf(stderr, "bake: failed to execute script\n");
                return status;
            }
        }
    }
    
    return 0;
}

static void __initialize_recipe_options(struct oven_recipe_options* options, struct recipe_part* part)
{
    options->name          = part->name;
    options->relative_path = part->path;
    options->toolchain     = fridge_get_utensil_location(part->toolchain);
}

static void __destroy_recipe_options(struct oven_recipe_options* options)
{
    free((void*)options->toolchain);
}

static int __make_recipes(struct recipe* recipe)
{
    struct oven_recipe_options options;
    struct list_item*          item;
    int                        status;

    list_foreach(&recipe->parts, item) {
        struct recipe_part* part = (struct recipe_part*)item;

        __initialize_recipe_options(&options, part);
        status = oven_recipe_start(&options);
        __destroy_recipe_options(&options);

        if (status) {
            return status;
        }

        status = __make_recipe_steps(&part->steps);
        oven_recipe_end();

        if (status) {
            fprintf(stderr, "bake: failed to build recipe %s\n", part->name);
            return status;
        }
    }

    return 0;
}

static int __make_packs(struct recipe* recipe)
{
    struct oven_pack_options packOptions;
    struct list_item*        item;
    int                      status;

    // include ingredients marked for packing
    list_foreach(&recipe->ingredients, item) {
        struct recipe_ingredient* ingredient = (struct recipe_ingredient*)item;
        if (ingredient->include) {
            status = oven_include_filters(&ingredient->filters);
            if (status) {
                fprintf(stderr, "bake: failed to include ingredient %s\n", ingredient->ingredient.name);
                return status;
            }
        }
    }

    list_foreach(&recipe->packs, item) {
        struct recipe_pack* pack = (struct recipe_pack*)item;

        __initialize_pack_options(&packOptions, recipe, pack);
        status = oven_pack(&packOptions);
        if (status) {
            fprintf(stderr, "bake: failed to construct pack %s\n", pack->name);
            return status;
        }
    }
    return 0;
}

static void __cleanup_systems(int sig)
{
    (void)sig;
    printf("bake: termination requested, cleaning up\n");
    exit(0);
}

static void __debug(void)
{
    // wait for any key and then return
    printf("bake: press any key to continue\n");
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

static int __reset_steps(struct list* steps, const char* step, const char* name);

static int __step_depends_on(struct list* dependencies, const char* step)
{
    struct list_item* item;

    list_foreach(dependencies, item) {
        struct oven_value_item* value = (struct oven_value_item*)item;
        if (strcmp(value->value, step) == 0) {
            // OK this step depends on the step we are resetting
            // so reset this step too
            return 1;
        }
    }
    return 0;
}

static int __reset_depending_steps(struct list* steps, const char* name)
{
    struct list_item* item;
    int               status;

    list_foreach(steps, item) {
        struct recipe_step* recipeStep = (struct recipe_step*)item;

        // skip ourselves
        if (strcmp(recipeStep->name, name) != 0) {
            if (__step_depends_on(&recipeStep->depends, name)) {
                status = __reset_steps(steps, NULL, recipeStep->name);
                if (status) {
                    fprintf(stderr, "bake: failed to reset step %s\n", recipeStep->name);
                    return status;
                }
            }
        }
    }
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

static int __reset_steps(struct list* steps, const char* step, const char* name)
{
    struct list_item* item;
    int               status;

    list_foreach(steps, item) {
        struct recipe_step* recipeStep = (struct recipe_step*)item;
        if ((step && recipeStep->type == __string_to_step_type(step)) ||
            (name && strcmp(recipeStep->name, name) == 0)) {
            // this should be deleted
            status = oven_clear_recipe_checkpoint(recipeStep->name);
            if (status) {
                fprintf(stderr, "bake: failed to clear checkpoint %s\n", recipeStep->name);
                return status;
            }

            // clear dependencies
            status = __reset_depending_steps(steps, recipeStep->name);
        }
    }
    return 0;
}

static int __reset_recipe_steps(struct recipe* recipe, const char* step)
{
    struct oven_recipe_options options;
    struct list_item*          item;
    int                        status;

    // ok nothing was specifically requested
    if (strcmp(step, "run") == 0) {
        return 0;
    }

    list_foreach(&recipe->parts, item) {
        struct recipe_part* part = (struct recipe_part*)item;

        __initialize_recipe_options(&options, part);
        status = oven_recipe_start(&options);
        __destroy_recipe_options(&options);

        if (status) {
            return status;
        }

        status = __reset_steps(&part->steps, step, NULL);
        oven_recipe_end();

        if (status) {
            fprintf(stderr, "bake: failed to build recipe %s\n", part->name);
            return status;
        }
    }

    return 0;
}

int run_main(int argc, char** argv, char** envp, struct recipe* recipe)
{
    int   status;
    char* name = NULL;
    char* arch = CHEF_ARCHITECTURE_STR;
    char* step = "run";
    int   debug = 0;

    // catch CTRL-C
    signal(SIGINT, __cleanup_systems);

    // handle individual help command
    if (argc > 1) {
        for (int i = 1; i < argc; i++) {
            if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
                __print_help();
                return 0;
            } else if (!strcmp(argv[i], "-n") || !strcmp(argv[i], "--name")) {
                if (i + 1 < argc) {
                    name = argv[i + 1];
                } else {
                    fprintf(stderr, "bake: missing argument for option: %s\n", argv[i]);
                    return 1;
                }
            } else if (!strcmp(argv[i], "-d") || !strcmp(argv[i], "--debug")) {
                debug = 1;
            } else if (!strcmp(argv[i], "-a") || !strcmp(argv[i], "--arch")) {
                if (i + 1 < argc) {
                    arch = argv[i + 1];
                } else {
                    fprintf(stderr, "bake: missing argument for option: %s\n", argv[i]);
                    return 1;
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

    status = fridge_initialize();
    if (status != 0) {
        fprintf(stderr, "bake: failed to initialize fridge\n");
        return -1;
    }
    atexit(fridge_cleanup);

    status = chefclient_initialize();
    if (status != 0) {
        fprintf(stderr, "bake: failed to initialize chef client\n");
        return -1;
    }
    atexit(chefclient_cleanup);

    status = __prep_ingredients(recipe);
    if (status) {
        fprintf(stderr, "bake: failed to fetch ingredients: %s\n", strerror(errno));
        return -1;
    }

    status = oven_initialize(envp, arch, name, fridge_get_prep_directory());
    if (status) {
        fprintf(stderr, "bake: failed to initialize oven: %s\n", strerror(errno));
        return -1;
    }
    atexit(oven_cleanup);
    
    status = __reset_recipe_steps(recipe, step);
    if (status) {
        fprintf(stderr, "bake: failed to reset steps: %s\n", strerror(errno));
        return -1;
    }

    status = __make_recipes(recipe);
    if (status) {
        fprintf(stderr, "bake: failed to make recipes\n");
        if (debug) {
            __debug();
        }
        return -1;
    }

    if (strcmp(step, "run") == 0 || strcmp(step, "pack") == 0) {
        status = __make_packs(recipe);
        if (status) {
            fprintf(stderr, "bake: failed to construct packs\n");
            if (debug) {
                __debug();
            }
            return -1;
        }
    }

    return status;
}
