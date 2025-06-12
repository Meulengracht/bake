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
 */

#include <chef/client.h>
#include <chef/api/package.h>
#include <chef/fridge.h>
#include <chef/platform.h>
#include <chef/recipe.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chef-config.h"
#include "commands.h"

int fridge_list_main(int argc, char** argv, char** envp, struct bake_command_options* options) { return 0; }
int fridge_update_main(int argc, char** argv, char** envp, struct bake_command_options* options) { return 0; }
int fridge_remove_main(int argc, char** argv, char** envp, struct bake_command_options* options) { return 0; }
int fridge_clean_main(int argc, char** argv, char** envp, struct bake_command_options* options) { return 0; }

struct command_handler {
    char* name;
    int (*handler)(int argc, char** argv, char** envp, struct bake_command_options* options);
};

static struct command_handler g_commands[] = {
    { "list",   fridge_list_main },
    { "update", fridge_update_main },
    { "remove", fridge_remove_main },
    { "clean",  fridge_clean_main }
};

static void __print_help(void)
{
    printf("Usage: bake fridge <command> [options]\n");
    printf("  This sub-command allows some management of the fridge for the current\n");
    printf("  user. Ingredients are automatically added, however unless the recipe requires\n");
    printf("  specific versions ingredients may need to be manually refreshed.\n\n");
    printf("  We also allow removal, cleaning and to list stored ingredients.\n\n");
    printf("Commands:\n");
    printf("  list      go through the configuration wizard\n");
    printf("  update    executes a recipe remotely\n");
    printf("  remove    executes a recipe remotely\n");
    printf("  clean     resumes execution of a recipe running remotely\n");
    printf("\n");
    printf("Options:\n");
    printf("  --version\n");
    printf("      Print the version of bake\n");
    printf("  -h,  --help\n");
    printf("      Shows this help message\n");
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

static int __resolve_ingredient(const char* publisher, const char* package, const char* platform, const char* arch, const char* channel, struct chef_version* version, const char* path, int* revisionDownloaded)
{
    struct chef_download_params downloadParams;
    int                         status;

    // initialize download params
    downloadParams.publisher = publisher;
    downloadParams.package   = package;
    downloadParams.platform  = platform;
    downloadParams.arch      = arch;
    downloadParams.channel   = channel;
    downloadParams.version   = version; // may be null, will just get latest

    status = chefclient_pack_download(&downloadParams, path);
    if (status == 0) {
        *revisionDownloaded = downloadParams.revision;
    }
    return status;
}

int fridge_main(int argc, char** argv, char** envp, struct bake_command_options* options)
{
    struct command_handler* command = NULL;
    int                     i;
    int                     status;

    status = fridge_initialize(&(struct fridge_parameters) {
        .platform = CHEF_PLATFORM_STR,
        .architecture = CHEF_ARCHITECTURE_STR,
        .backend = {
            .resolve_ingredient = __resolve_ingredient
        }
    });
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

    // handle individual commands as well as --help and --version
    // locate the fridge command on the cmdline
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "fridge")) {
            i++;
            break;
        }
    }

    if (i < argc) {
        if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            __print_help();
            return 0;
        }

        if (!strcmp(argv[i], "--version")) {
            printf("bake: version " PROJECT_VER "\n");
            return 0;
        }

        command = __get_command(argv[i]);
    }

    if (command == NULL) {
        fprintf(stderr, "bake: command must be supplied for 'bake fridge'\n");
        __print_help();
        return -1;
    }
    return command->handler(argc, argv, envp, options);
}
