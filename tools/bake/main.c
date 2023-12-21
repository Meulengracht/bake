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
 */

#include <errno.h>
#include <chef/platform.h>
#include <recipe.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vlog.h>
#include "chef-config.h"

extern int init_main(int argc, char** argv, char** envp, struct recipe* recipe);
extern int fetch_main(int argc, char** argv, char** envp, struct recipe* recipe);
extern int run_main(int argc, char** argv, char** envp, struct recipe* recipe);
extern int clean_main(int argc, char** argv, char** envp, struct recipe* recipe);

struct command_handler {
    char* name;
    int (*handler)(int argc, char** argv, char** envp, struct recipe* recipe);
};

static struct command_handler g_commands[] = {
    { "init",     init_main },
    { "fetch",    fetch_main },
    { "run",      run_main },
    { "generate", run_main },
    { "build",    run_main },
    { "script",   run_main },
    { "pack",     run_main },
    { "clean",    clean_main }
};

static void __print_help(void)
{
    printf("Usage: bake <command> <recipe> [options]\n");
    printf("\n");
    printf("Commands:\n");
    printf("  init        initializes a new recipe in the current directory\n");
    printf("  fetch       refreshes/fetches all ingredients\n");
    printf("  run         runs all recipe steps that have not already been completed\n");
    printf("  generate    run configure step and its dependencies\n");
    printf("  build       run the build step and its dependencies\n");
    printf("  script      run the script step and its dependencies\n");
    printf("  pack        run the pack step\n");
    printf("  clean       cleanup all build and intermediate directories\n");
    printf("\n");
    printf("Options:\n");
    printf("  -h, --help\n");
    printf("      Print this help message\n");
    printf("  -v, --version\n");
    printf("      Print the version of bake\n");
}

static struct command_handler* __get_command(const char* command)
{
    for (int i = 0; i < sizeof(g_commands) / sizeof(struct command_handler); i++) {
        if (!strcmp(command, g_commands[i].name)) {
            return &g_commands[i];
        }
    }
    return NULL;
}

static int __read_recipe(char* path, void** bufferOut, size_t* lengthOut)
{
    FILE*  file;
    void*  buffer;
    size_t size, read;

    if (path == NULL) {
        errno = EINVAL;
        return -1;
    }

    file = fopen(path, "r");
    if (!file) {
        fprintf(stderr, "bake: failed to read recipe path: %s\n", path);
        return -1;
    }

    fseek(file, 0, SEEK_END);
    size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    buffer = malloc(size);
    if (!buffer) {
        fprintf(stderr, "bake: failed to allocate memory for recipe: %s\n", strerror(errno));
        fclose(file);
        return -1;
    }

    read = fread(buffer, 1, size, file);
    if (read < size) {
        fprintf(stderr, "bake: failed to read recipe: %s\n", strerror(errno));
        fclose(file);
        return -1;
    }
    
    fclose(file);

    *bufferOut = buffer;
    *lengthOut = size;
    return 0;
}

static int __is_osbase(const char* name)
{
    if (strcmp(name, "vali/linux-1") == 0) {
        return 0;
    }
    return -1;
}

static int __needs_base(struct recipe* recipe)
{
    struct list_item* i;

    list_foreach(&recipe->packs, i) {
        struct recipe_pack* pack = (struct recipe_pack*)i;
        if (pack->type == CHEF_PACKAGE_TYPE_OSBASE) {
            return 0;
        }
    }
    return 1;
}

static int __add_ingredient(struct recipe* recipe, const char* name)
{
    struct recipe_ingredient* ingredient;

    ingredient = malloc(sizeof(struct recipe_ingredient));
    if (ingredient == NULL) {
        return -1;
    }

    memset(ingredient, 0, sizeof(struct recipe_ingredient));
    ingredient->ingredient.name = strdup(name);
    ingredient->ingredient.arch = strdup(CHEF_ARCHITECTURE_STR);
    ingredient->ingredient.platform = strdup(CHEF_PLATFORM_STR);
    if (ingredient->ingredient.name == NULL || ingredient->ingredient.arch == NULL || ingredient->ingredient.platform == NULL) {
        free((void*)ingredient->ingredient.name);
        free((void*)ingredient->ingredient.arch);
        free((void*)ingredient->ingredient.platform);
        free(ingredient);
        return -1;
    }

    ingredient->ingredient.channel = "devel"; // TODO: should be something else
    list_add(&recipe->ingredients, &ingredient->list_header);
    return 0;
}

static int __add_osbase(struct recipe* recipe)
{
    char nameBuffer[32];
    snprintf(&nameBuffer[0], sizeof(nameBuffer), "vali/%s-1", CHEF_PLATFORM_STR);
    return __add_ingredient(recipe, &nameBuffer[0]);
}

static int __add_devel(struct recipe* recipe)
{
    char nameBuffer[32];
    snprintf(&nameBuffer[0], sizeof(nameBuffer), "vali/devel-1");
    return __add_ingredient(recipe, &nameBuffer[0]);
}

static int __add_implicit_ingredients(struct recipe* recipe)
{
    struct list_item* i;
    int               needsOs = __needs_base(recipe);

    list_foreach(&recipe->ingredients, i) {
        struct recipe_ingredient* ingredient = (struct recipe_ingredient*)i;
        if (__is_osbase(ingredient->ingredient.name) == 0) {
            needsOs = 0;
        }
    }

    if (needsOs && __add_osbase(recipe)) {
        return -1;
    }
    return 0;
}

int main(int argc, char** argv, char** envp)
{
    struct command_handler* command    = &g_commands[2]; // run step is default
    struct recipe*          recipe     = NULL;
    char*                   recipePath = NULL;
    char*                   arch       = CHEF_ARCHITECTURE_STR;
    void*                   buffer;
    size_t                  length;
    int                     result;
    
    // first argument must be the command if not --help or --version
    if (argc > 1) {
        if (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")) {
            __print_help();
            return 0;
        }

        if (!strcmp(argv[1], "-v") || !strcmp(argv[1], "--version")) {
            printf("bake: version " PROJECT_VER "\n");
            return 0;
        }

        command = __get_command(argv[1]);
        if (!command) {
            struct platform_stat stats;
            // was a file passed? Then it was the recipe, and we assume
            // that the run command should be run.
            if (platform_stat(argv[1], &stats) == 0) {
                command = &g_commands[2];
                recipePath = argv[1];
            } else {
                fprintf(stderr, "bake: invalid command %s\n", argv[1]);
                return -1;
            }
        }

        if (argc > 2) {
            for (int i = 2; i < argc; i++) {
                if (!strcmp(argv[i], "-a") || !strcmp(argv[i], "--arch")) {
                    if (i + 1 < argc) {
                        arch = argv[i + 1];
                    } else {
                        fprintf(stderr, "bake: missing argument for option: %s\n", argv[i]);
                        return -1;
                    }
                } else if (argv[i][0] != '-') {
                    recipePath = argv[i];
                }
            }
        }
    }

    if (recipePath != NULL) {
        result = __read_recipe(recipePath, &buffer, &length);
        if (!result) {
            result = recipe_parse(buffer, length, &recipe);
            free(buffer);
            if (result) {
                fprintf(stderr, "bake: failed to parse recipe\n");
                return result;
            }
            
            result = __add_implicit_ingredients(recipe);
            if (result) {
                fprintf(stderr, "bake: failed to add implicit ingredients\n");
                return result;
            }
        }
    }

    vlog_initialize();
    vlog_set_level(VLOG_LEVEL_DEBUG);
    vlog_add_output(stdout, 0);
    result = command->handler(argc, argv, envp, recipe);
    recipe_destroy(recipe);
    vlog_cleanup();
    return result;
}
