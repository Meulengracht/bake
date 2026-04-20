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

#include <chef/cli.h>
#include <stdio.h>
#include <string.h>
#include "chef-config.h"

extern int install_main(int argc, char** argv);
extern int remove_main(int argc, char** argv);
extern int update_main(int argc, char** argv);
extern int list_main(int argc, char** argv);
extern int config_main(int argc, char** argv);

struct command_handler {
    char* name;
    int (*handler)(int argc, char** argv);
};

static struct command_handler g_commands[] = {
    { "install", install_main },
    { "remove",  remove_main },
    { "update",  update_main },
    { "list",    list_main },
    { "config",  config_main }
};

enum serve_global_action {
    SERVE_GLOBAL_ACTION_NONE,
    SERVE_GLOBAL_ACTION_HELP,
    SERVE_GLOBAL_ACTION_VERSION
};

struct serve_global_options {
    enum serve_global_action action;
};

static void __print_help(void)
{
    printf("Usage: serve [global-options] <command> [command-options]\n");
    printf("\n");
    printf("Commands:\n");
    printf("  install     install a new package\n");
    printf("  remove      remove a previously installed package\n");
    printf("  update      update an installed package or do a full update\n");
    printf("  list        list all installed packages\n");
    printf("  config      view or change served configuration values\n");
    printf("\n");
    printf("Global Options:\n");
    printf("  -h, --help\n");
    printf("      Print this help message\n");
    printf("  -v, --version\n");
    printf("      Print the version of serve\n");
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

static int __parse_global_option(int argc, char** argv, int* index, void* context)
{
    struct serve_global_options* options = context;

    if (__cli_is_help_switch(argv[*index])) {
        options->action = SERVE_GLOBAL_ACTION_HELP;
        return CLI_PARSE_RESULT_HANDLED;
    }
    if (!strcmp(argv[*index], "-v") || !strcmp(argv[*index], "--version")) {
        options->action = SERVE_GLOBAL_ACTION_VERSION;
        return CLI_PARSE_RESULT_HANDLED;
    }
    if (argv[*index][0] == '-') {
        fprintf(stderr, "serve: invalid global option %s\n", argv[*index]);
        return CLI_PARSE_RESULT_ERROR;
    }
    return CLI_PARSE_RESULT_UNHANDLED;
}

int main(int argc, char** argv, char** envp)
{
    struct serve_global_options options = { 0 };
    struct command_handler* command = NULL;
    int                     commandIndex = argc;
    int                     result;

    (void)envp;

    result = __cli_parse_staged_global_options(argc, argv, __parse_global_option, &options, &commandIndex);
    if (result != 0) {
        return -1;
    }

    if (options.action == SERVE_GLOBAL_ACTION_HELP) {
        __print_help();
        return 0;
    }
    if (options.action == SERVE_GLOBAL_ACTION_VERSION) {
        printf("serve: version " PROJECT_VER "\n");
        return 0;
    }
    if (commandIndex >= argc) {
        __print_help();
        return 0;
    }

    command = __get_command(argv[commandIndex]);
    if (!command) {
        fprintf(stderr, "serve: invalid command %s\n", argv[commandIndex]);
        return -1;
    }

    result = command->handler(argc - commandIndex, &argv[commandIndex]);
    if (result != 0) {
        return result;
    }
    return 0;
}
