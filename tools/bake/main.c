/**
 * Copyright 2024, Philip Meulengracht
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
#include <chef/recipe.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <vlog.h>
#include "chef-config.h"
#include "commands/commands.h"

extern int init_main(int argc, char** argv, char** envp, struct bake_command_options* options);
extern int fetch_main(int argc, char** argv, char** envp, struct bake_command_options* options);
extern int run_main(int argc, char** argv, char** envp, struct bake_command_options* options);
extern int clean_main(int argc, char** argv, char** envp, struct bake_command_options* options);

struct command_handler {
    char* name;
    int (*handler)(int argc, char** argv, char** envp, struct bake_command_options* options);
};

static struct command_handler g_commands[] = {
    { "init",     init_main },
    { "fetch",    fetch_main },
    { "run",      run_main },
    { "clean",    clean_main }
};

static void __print_help(void)
{
    printf("Usage: bake <command> <recipe> [options]\n");
    printf("\n");
    printf("If no recipe is specified, it will search for default recipe names as follows:\n");
    printf("  chef/recipe.yaml\n");
    printf("\n");
    printf("Commands:\n");
    printf("  init        initializes a new recipe in the current directory\n");
    printf("  fetch       refreshes/fetches all ingredients\n");
    printf("  run         runs all recipe steps that have not already been completed\n");
    printf("  clean       cleanup all build and intermediate directories\n");
    printf("\n");
    printf("Options:\n");
    printf("  -h, --help\n");
    printf("      Print this help message\n");
    printf("  -v, --version\n");
    printf("      Print the version of bake\n");
    printf("  -cc, --cross-compile\n");
    printf("      Cross-compile for another platform or/and architecture. This switch\n");
    printf("      can be used with two different formats, either just like\n");
    printf("      --cross-compile=arch or --cross-compile=platform/arch\n");
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

static int __add_ingredient(struct recipe* recipe, const char* name)
{
    struct recipe_ingredient* ingredient;

    ingredient = malloc(sizeof(struct recipe_ingredient));
    if (ingredient == NULL) {
        return -1;
    }

    memset(ingredient, 0, sizeof(struct recipe_ingredient));
    ingredient->name = platform_strdup(name);
    ingredient->type = RECIPE_INGREDIENT_TYPE_HOST;
    ingredient->source.type = INGREDIENT_SOURCE_TYPE_REPO;
    if (ingredient->name == NULL) {
        free(ingredient);
        return -1;
    }

    ingredient->channel = "devel"; // TODO: should be something else
    list_add(&recipe->environment.host.ingredients, &ingredient->list_header);
    return 0;
}

static int __add_osbase(struct recipe* recipe)
{
    char nameBuffer[32];
    snprintf(&nameBuffer[0], sizeof(nameBuffer), "vali/%s-1", CHEF_PLATFORM_STR);
    return __add_ingredient(recipe, &nameBuffer[0]);
}

static int __add_implicit_ingredients(struct recipe* recipe)
{
    struct list_item* i;
    int               needsOs = recipe->environment.host.base;

    list_foreach(&recipe->environment.host.ingredients, i) {
        struct recipe_ingredient* ingredient = (struct recipe_ingredient*)i;
        if (__is_osbase(ingredient->name) == 0) {
            needsOs = 0;
        }
    }

#if defined(__MOLLENOS__)
    if (needsOs && __add_osbase(recipe)) {
        return -1;
    }
#endif
    return 0;
}

static const char* __find_default_recipe(void)
{
    struct platform_stat stats;
    if (platform_stat("chef/recipe.yaml", &stats) == 0) {
        return "chef/recipe.yaml";
    }
    return NULL;
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
        fprintf(stderr, "bake: invalid format of %s (must be -cc=... or --cross-compile=...)\n", value);
        errno = EINVAL;
        return -1;
    }

    // skip the '='
    equal++;

    separator = strchr(equal, '/');
    if (separator) {
        *platformOut = platform_strndup(equal, separator - equal);
        *archOut     = platform_strdup(separator + 1);
    } else {
        *platformOut = platform_strdup(CHEF_PLATFORM_STR);
        *archOut     = platform_strdup(equal);
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
        fprintf(stderr, "bake: could not get current working directory, buffer too small?\n");
        free(cwd);
        return -1;
    }
    *bufferOut = cwd;
    return 0;
}

int main(int argc, char** argv, char** envp)
{
    struct command_handler* command     = &g_commands[2]; // run step is default
    struct bake_command_options options = { 0 };
    char*                   recipePath  = NULL;
    void*                   buffer;
    size_t                  length;
    int                     status;
    
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
                if (!strncmp(argv[i], "-cc", 3) || !strncmp(argv[i], "--cross-compile", 15)) {
                    // THIS ALLOCS MEMORY, WE NEED TO HANDLE THIS
                    status = __parse_cc_switch(argv[i], &options.platform, &options.architecture);
                    if (status) {
                        fprintf(stderr, "bake: invalid format: %s\n", argv[i]);
                        return status;
                    }
                } else if (argv[i][0] != '-') {
                    recipePath = argv[i];
                }
            }
        }
    }

    // get the current working directory
    status = __get_cwd((char**)&options.cwd);
    if (status) {
        return -1;
    }

    if (recipePath == NULL) {
        recipePath = (char*)__find_default_recipe();
    }

    if (recipePath != NULL) {
        status = __read_recipe(recipePath, &buffer, &length);
        if (!status) {
            status = recipe_parse(buffer, length, &options.recipe);
            free(buffer);
            if (status) {
                fprintf(stderr, "bake: failed to parse recipe\n");
                return status;
            }
            
            status = __add_implicit_ingredients(options.recipe);
            if (status) {
                fprintf(stderr, "bake: failed to add implicit ingredients\n");
                return status;
            }

            status = recipe_validate_target(options.recipe, &options.platform, &options.architecture);
            if (status) {
                return -1;
            }
            printf("bake: target platform: %s\n", options.platform);
            printf("bake: target architecture: %s\n", options.architecture);
        }
    }

    // initialize the logging system
    vlog_initialize();
    // TODO switch to trace by default, allow -v for debug
    vlog_set_level(VLOG_LEVEL_DEBUG);
    vlog_add_output(stdout);

    status = command->handler(argc, argv, envp, &options);
    recipe_destroy(options.recipe);
    vlog_cleanup();
    return status;
}
