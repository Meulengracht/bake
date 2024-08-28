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

extern int source_main(int argc, char** argv, char** envp, struct bakectl_command_options* options);
extern int build_main(int argc, char** argv, char** envp, struct bakectl_command_options* options);
extern int clean_main(int argc, char** argv, char** envp, struct bakectl_command_options* options);

struct command_handler {
    char* name;
    int (*handler)(int argc, char** argv, char** envp, struct bakectl_command_options* options);
};

static struct command_handler g_commands[] = {
    { "source", source_main },
    { "build",  build_main },
    { "clean",  clean_main },
};

static void __print_help(void)
{
    printf("Usage: bakectl <command> [options]\n");
    printf("\n");
    printf("The control tool to help facilitate certain aspects of the build/clean process\n");
    printf("of chef projects. This utility must only be invoked by the main binary (bake).\n");
    printf("\n");
    printf("Commands:\n");
    printf("  source      prepares the source of the specified part and step\n");
    printf("  build       runs the build backend of the specified part and step\n");
    printf("  clean       runs the clean backend of the specified part and step\n");
    printf("\n");
    printf("Options:\n");
    printf("  -p, --project\n");
    printf("      Root path of the project\n");
    printf("  -r, --recipe\n");
    printf("      Relative path (of --project) or absolute path to the recipe for the project\n");
    printf("  -v..\n");
    printf("      Controls the verbosity of bakectl\n");
    printf("      --version\n");
    printf("      Print the version of bakectl\n");
    printf("  -h, --help\n");
    printf("      Print this help message\n");
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
        fprintf(stderr, "bakectl: failed to read recipe path: %s\n", path);
        return -1;
    }

    fseek(file, 0, SEEK_END);
    size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    buffer = malloc(size);
    if (!buffer) {
        fprintf(stderr, "bakectl: failed to allocate memory for recipe: %s\n", strerror(errno));
        fclose(file);
        return -1;
    }

    read = fread(buffer, 1, size, file);
    if (read < size) {
        fprintf(stderr, "bakectl: failed to read recipe: %s\n", strerror(errno));
        fclose(file);
        return -1;
    }
    
    fclose(file);

    *bufferOut = buffer;
    *lengthOut = size;
    return 0;
}

int main(int argc, char** argv, char** envp)
{
    struct command_handler*        command    = &g_commands[0];
    struct bakectl_command_options options    = { 0 };
    char*                          recipePath = NULL;
    void*                          buffer;
    size_t                         length;
    int                            status;
    int                            logLevel = VLOG_LEVEL_TRACE;
    
    // first argument must be the command if not --help or --version
    if (argc > 1) {
        if (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")) {
            __print_help();
            return 0;
        }

        if (!strcmp(argv[1], "--version")) {
            printf("bakectl: version " PROJECT_VER "\n");
            return 0;
        }

        command = __get_command(argv[1]);
        if (command == NULL) {
            fprintf(stderr, "bakectl: invalid command %s\n", argv[1]);
            return -1;
        }

        if (argc > 2) {
            for (int i = 2; i < argc; i++) {
                if (!strcmp(argv[i], "-p") || !strcmp(argv[i], "--project")) {
                    options.cwd = argv[i + 1];
                    i++;
                } else if (!strcmp(argv[i], "-r") || !strcmp(argv[i], "--recipe")) {
                    recipePath = argv[i + 1];
                    i++;
                } else if (!strcmp(argv[i], "-s") || !strcmp(argv[i], "--step")) {
                    status = recipe_parse_part_step(argv[i + 1], (char**)&options.part, (char**)&options.step);
                    if (status) {
                        fprintf(stderr, "bakectl: failed to parse %s\n", argv[i + 1]);
                        return status;
                    }
                } else if (!strncmp(argv[i], "-v", 2)) {
                    int li = 1;
                    while (argv[i][li++] == 'v') {
                        logLevel++;
                    }
                }
            }
        }
    }

    if (options.cwd == NULL) {
        fprintf(stderr, "bakectl: no project path specified\n");
        return -1;
    }

    // Switch to the project root as working directory
    status = platform_chdir(options.cwd);
    if (status) {
        fprintf(stderr, "bakectl: failed to switch directory to %s\n", options.cwd);
        return -1;
    }

    if (recipePath != NULL) {
        status = __read_recipe(recipePath, &buffer, &length);
        if (!status) {
            status = recipe_parse(buffer, length, &options.recipe);
            free(buffer);
            if (status) {
                fprintf(stderr, "bakectl: failed to parse recipe\n");
                return status;
            }
        }
    }

    // initialize the logging system
    vlog_initialize((enum vlog_level)logLevel);
    vlog_set_output_options(stdout, VLOG_OUTPUT_OPTION_NODECO);

    status = command->handler(argc, argv, envp, &options);
    recipe_destroy(options.recipe);
    vlog_cleanup();
    return status;
}
