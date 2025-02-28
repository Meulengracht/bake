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
#include <chef/dirs.h>
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
extern int run_main(int argc, char** argv, char** envp, struct bake_command_options* options);
extern int clean_main(int argc, char** argv, char** envp, struct bake_command_options* options);
extern int fridge_main(int argc, char** argv, char** envp, struct bake_command_options* options);
extern int remote_main(int argc, char** argv, char** envp, struct bake_command_options* options);

struct command_handler {
    char* name;
    int (*handler)(int argc, char** argv, char** envp, struct bake_command_options* options);
};

static struct command_handler g_commands[] = {
    { "init",   init_main },
    { "build",  run_main },
    { "clean",  clean_main },
    { "fridge", fridge_main },
    { "remote", remote_main }
};

static void __print_help(void)
{
    printf("Usage: bake <command> <recipe> [options]\n");
    printf("\n");
    printf("If no recipe is specified, it will search for default recipe names as follows:\n");
    printf("  chef/recipe.yaml\n");
    printf("  recipe.yaml\n");
    printf("\n");
    printf("Commands:\n");
    printf("  init\n");
    printf("              initializes a new recipe in the current directory\n");
    printf("  build\n");
    printf("              builds the provided (or inferred) bake recipe\n");
    printf("  clean\n");
    printf("              cleanup all build and intermediate directories\n");
    printf("  remote {init, build, resume, download}\n");
    printf("              used for building recipes remotely for any given configured\n");
    printf("              build server, parallel builds can be initiated for multiple\n");
    printf("              architectures by using the --archs switch\n");
    printf("  fridge {list, update, remove, clean}\n");
    printf("              manage ingredients used for building\n");
    printf("\n");
    printf("Options:\n");
    printf("  -p, --platform\n");
    printf("      Cross-compile for a specific platform.\n");
    printf("  -a, --archs\n");
    printf("      Cross-compile for specific architectures, when doing local builds only\n");
    printf("      a single architecture can be set at one time. When doing remote builds\n");
    printf("      multiple architectures can be specified like --archs=amd64,arm64 to build\n");
    printf("      multiple times in parallel. If not set, the host architecture is used.\n");
    printf("  --version\n");
    printf("      Print the version of bake\n");
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
    if (platform_stat("recipe.yaml", &stats) == 0) {
        return "recipe.yaml";
    }
    return NULL;
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

static int __file_exists(const char* path)
{
    struct platform_stat stats;
    return platform_stat(path, &stats) == 0 ? 1 : 0;
}

#ifdef CHEF_AS_SNAP
#include <stdlib.h>
static unsigned int __get_snap_uid(void)
{
    char* uidstr = getenv("SNAP_UID");
    if (uidstr == NULL) {
        // fallback
        return getuid();
    }
    return (unsigned int)atoi(uidstr); 
}
#endif

int main(int argc, char** argv, char** envp)
{
    struct command_handler*     command = &g_commands[1]; // build step is default
    struct bake_command_options options = { 0 };
    void*                       buffer;
    size_t                      length;
    int                         status;
    int                         logLevel = VLOG_LEVEL_TRACE;
    
#if __linux__
    // make sure we're running with root privileges
    if (geteuid() != 0 || getegid() != 0) {
        fprintf(stderr, "bake: should be executed with root privileges, aborting.\n");
        errno = EPERM;
        return -1;
    }

    // make sure we're not actually running as root
#ifdef CHEF_AS_SNAP
    if (__get_snap_uid() == 0) {
        fprintf(stderr, "bake: should not be run as root, aborting.\n");
        errno = EPERM;
        return -1;
    }
#else
    if (getuid() == 0 || getgid() == 0) {
        fprintf(stderr, "bake: should not be run as root, aborting.\n");
        errno = EPERM;
        return -1;
    }
#endif
#endif

    // first argument must be the command if not --help or --version
    if (argc > 1) {
        if (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")) {
            __print_help();
            return 0;
        }

        if (!strcmp(argv[1], "--version")) {
            printf("bake: version " PROJECT_VER "\n");
            return 0;
        }

        command = __get_command(argv[1]);
        if (command == NULL) {
            // was a file passed? Then it was the recipe, and we assume
            // that the run command should be run.
            if (__file_exists(argv[1]) == 0) {
                command = &g_commands[2];
                options.recipe_path = argv[1];
            } else {
                fprintf(stderr, "bake: invalid command %s\n", argv[1]);
                return -1;
            }
        }

        if (argc > 2) {
            for (int i = 2; i < argc; i++) {
                if (!__parse_string_switch(argv, argc, &i, "-p", 2, "--platform", 10, NULL, (char**)&options.platform)) {
                    continue;
                } else if (!__parse_stringv_switch(argv, argc, &i, "-a", 2, "--archs", 7, NULL, &options.architectures)) {
                    continue;
                } else if (!strncmp(argv[i], "-v", 2)) {
                    int li = 1;
                    while (argv[i][li++] == 'v') {
                        logLevel++;
                    }
                } else if (argv[i][0] != '-') {
                    if (__file_exists(argv[i])) {
                        options.recipe_path = argv[i];
                    }
                }
            }
        }
    }

    // get the current working directory
    status = __get_cwd((char**)&options.cwd);
    if (status) {
        return -1;
    }

    if (options.recipe_path == NULL) {
        options.recipe_path = (char*)__find_default_recipe();
    }

    if (options.recipe_path != NULL) {
        status = platform_readfile(options.recipe_path, &buffer, &length);
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

            status = recipe_ensure_target(options.recipe, &options.platform, &options.architectures);
            if (status) {
                return -1;
            }
        } else {
            fprintf(stderr, "bake: failed to read recipe: %s\n", options.recipe_path);
        }
    }

    // initialize the logging system
    vlog_initialize(logLevel);

    // initialize directories
    status = chef_dirs_initialize();
    if (status) {
        fprintf(stderr, "bake: failed to initialize directories\n");
        return -1;
    }

    status = command->handler(argc, argv, envp, &options);
    recipe_destroy(options.recipe);
    vlog_cleanup();
    return status;
}
