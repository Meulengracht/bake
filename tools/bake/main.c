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
#include <libplatform.h>
#include <recipe.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
    size_t size;

    if (path == NULL) {
        errno = EINVAL;
        return -1;
    }

    file = fopen(path, "r");
    if (!file) {
        return -1;
    }

    fseek(file, 0, SEEK_END);
    size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    buffer = malloc(size);
    if (!buffer) {
        fclose(file);
        return -1;
    }

    (void)fread(buffer, size, 1, file);
    fclose(file);

    *bufferOut = buffer;
    *lengthOut = size;
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
            // was a file passed? Then it was the recipe and we assume
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
                        return 1;
                    }
                } else if (argv[i][0] != '-') {
                    recipePath = argv[i];
                }
            }
        }
    }

    result = __read_recipe(recipePath, &buffer, &length);
    if (!result) {
        result = recipe_parse(buffer, length, &recipe);
        free(buffer);
        if (result) {
            fprintf(stderr, "bake: failed to parse recipe\n");
            return result;
        }
    }

    result = command->handler(argc, argv, envp, recipe);
    recipe_destroy(recipe);
    if (result != 0) {
        return result;
    }
    return 0;
}
