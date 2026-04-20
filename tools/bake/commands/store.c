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
#include <chef/store-default.h>
#include <chef/platform.h>
#include <chef/recipe.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chef-config.h"
#include "commands.h"

int store_list_main(int argc, char** argv, char** envp, struct bake_command_options* options) { return 0; }
int store_update_main(int argc, char** argv, char** envp, struct bake_command_options* options) { return 0; }
int store_remove_main(int argc, char** argv, char** envp, struct bake_command_options* options) { return 0; }
int store_clean_main(int argc, char** argv, char** envp, struct bake_command_options* options) { return 0; }

struct command_handler {
    char* name;
    int (*handler)(int argc, char** argv, char** envp, struct bake_command_options* options);
};

static struct command_handler g_commands[] = {
    { "list",   store_list_main },
    { "update", store_update_main },
    { "remove", store_remove_main },
    { "clean",  store_clean_main }
};

static void __print_help(void)
{
    printf("Usage: bake store <command> [options]\n");
    printf("  This sub-command allows some management of the store for the current\n");
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

int store_main(int argc, char** argv, char** envp, struct bake_command_options* options)
{
    struct command_handler* command = NULL;
    int                     status;

    if (argc < 2) {
        fprintf(stderr, "bake: command must be supplied for 'bake store'\n");
        __print_help();
        return -1;
    }

    if (__cli_is_help_switch(argv[1])) {
        __print_help();
        return 0;
    }
    if (!strcmp(argv[1], "--version")) {
        printf("bake: version " PROJECT_VER "\n");
        return 0;
    }

    status = store_initialize(&(struct store_parameters) {
        .platform = CHEF_PLATFORM_STR,
        .architecture = CHEF_ARCHITECTURE_STR,
        .backend = g_store_default_backend
    });
    if (status != 0) {
        fprintf(stderr, "bake: failed to initialize store\n");
        return -1;
    }
    atexit(store_cleanup);

    status = chefclient_initialize();
    if (status != 0) {
        fprintf(stderr, "bake: failed to initialize chef client\n");
        return -1;
    }
    atexit(chefclient_cleanup);

    command = __get_command(argv[1]);

    if (command == NULL) {
        fprintf(stderr, "bake: command must be supplied for 'bake store'\n");
        __print_help();
        return -1;
    }
    return command->handler(argc - 1, &argv[1], envp, options);
}
