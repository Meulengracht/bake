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
 * VaFS TODOs:
 * - Block caching system (unpack speeds of very large images are insanely slow)
 * - FS statistics (file count, total uncompressed size of files) to support unpack progress
 * Package System TODOs:
 * - toolchain support (in progress)
 * - serve protocol
 * - autotools backend
 * Application System TODOs:
 * - app commands
 * - icon support
 * - served system
 */

#include <chef/client.h>
#include <errno.h>
#include <liboven.h>
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
    printf("  -n, --name\n");
    printf("      The name for the produced container image (default: recipe-name)\n");
    printf("  -g, --regenerate\n");
    printf("      Cleans and reconfigures before building\n");
    printf("  -h, --help\n");
    printf("      Shows this help message\n");
}

static void __initialize_generator_options(struct oven_generate_options* options, struct recipe_step* step)
{
    options->profile     = NULL;
    options->system      = step->system;
    options->arguments   = &step->arguments;
    options->environment = &step->env_keypairs;
}

static void __initialize_build_options(struct oven_build_options* options, struct recipe_step* step)
{
    options->profile     = NULL;
    options->system      = step->system;
    options->arguments   = &step->arguments;
    options->environment = &step->env_keypairs;
}

static void __initialize_pack_options(struct oven_pack_options* options, char* name, struct recipe* recipe)
{
    options->name        = name;
    options->type        = recipe->type;
    options->description = recipe->project.description;
    options->version     = recipe->project.version;
    options->license     = recipe->project.license;
    options->author      = recipe->project.author;
    options->email       = recipe->project.email;
    options->url         = recipe->project.url;
}

static int __fetch_ingredients(struct recipe* recipe)
{
    struct list_item* item;
    int               status;

    if (recipe->ingredients.count == 0) {
        return 0;
    }

    // iterate through all ingredients
    printf("bake: preparing %i ingredients\n", recipe->ingredients.count);
    for (item = recipe->ingredients.head; item != NULL; item = item->next) {
        struct recipe_ingredient* ingredient = (struct recipe_ingredient*)item;
        
        // fetch the ingredient
        status = fridge_use_ingredient(&ingredient->ingredient);
        if (status != 0) {
            fprintf(stderr, "bake: failed to fetch ingredient %s\n", ingredient->ingredient.name);
            return -1;
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

void __cleanup_systems(int sig)
{
    (void)sig;
    printf("bake: termination requested, cleaning up\n");
    oven_cleanup();
    chefclient_cleanup();
    fridge_cleanup();
}

int pack_main(int argc, char** argv, char** envp, struct recipe* recipe)
{
    struct oven_pack_options packOptions;
    int                      status;
    char*                    name = NULL;
    int                      regenerate = 0;

    // catch CTRL-C
    signal(SIGINT, __cleanup_systems);

    // handle individual help command
    if (argc > 2) {
        for (int i = 2; i < argc; i++) {
            if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
                __print_help();
                return 0;
            } else if (!strcmp(argv[i], "-r") || !strcmp(argv[i], "--recipe")) {
                if (i + 1 < argc) {
                    if (name == NULL) {
                        // only set name if --name was not provided
                        name = argv[i + 1];
                    }
                } else {
                    fprintf(stderr, "bake: missing argument for option: %s\n", argv[i]);
                    return 1;
                }
            } else if (!strcmp(argv[i], "-n") || !strcmp(argv[i], "--name")) {
                if (i + 1 < argc) {
                    name = argv[i + 1];
                } else {
                    fprintf(stderr, "bake: missing argument for option: %s\n", argv[i]);
                    return 1;
                }
            } else if (!strcmp(argv[i], "-g") || !strcmp(argv[i], "--regenerate")) {
                regenerate = 1;
            }
        }
    }

    if (name == NULL) {
        name = "recipe.yaml";
    }

    if (recipe == NULL) {
        fprintf(stderr, "bake: no recipe provided\n");
        return 1;
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

    // fetch ingredients
    status = __fetch_ingredients(recipe);
    if (status) {
        fprintf(stderr, "bake: failed to fetch ingredients: %s\n", strerror(errno));
        return -1;
    }

    status = oven_initialize(envp, fridge_get_prep_directory());
    if (status) {
        fprintf(stderr, "bake: failed to initialize oven: %s\n", strerror(errno));
        return -1;
    }
    atexit(oven_cleanup);

    if (regenerate) {
        status = oven_reset();
        if (status) {
            fprintf(stderr, "bake: failed to reset oven: %s\n", strerror(errno));
            return -1;
        }
    }

    // build parts
    status = __make_recipes(recipe);
    if (status) {
        fprintf(stderr, "bake: failed to make recipe\n");
        return -1;
    }

    // pack it all together
    __initialize_pack_options(&packOptions, name, recipe);
    status = oven_pack(&packOptions);
    if (status) {
        fprintf(stderr, "bake: failed to pack target: %s\n", strerror(errno));
    }
    return status;
}
