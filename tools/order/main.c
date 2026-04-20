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

#include <chef/dirs.h>
#include <chef/cli.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "chef-config.h"
#include <vlog.h>

extern int account_main(int argc, char** argv);
extern int config_main(int argc, char** argv);
extern int package_main(int argc, char** argv);
extern int info_main(int argc, char** argv);
extern int find_main(int argc, char** argv);
extern int publish_main(int argc, char** argv);

struct command_handler {
    char* name;
    int (*handler)(int argc, char** argv);
};

static struct command_handler g_commands[] = {
    { "account",  account_main },
    { "config",   config_main },
    { "package",  package_main },
    { "info",     info_main },
    { "find",     find_main },
    { "publish",  publish_main }
};

enum order_global_action {
    ORDER_GLOBAL_ACTION_NONE,
    ORDER_GLOBAL_ACTION_HELP,
    ORDER_GLOBAL_ACTION_VERSION
};

struct order_global_options {
    enum order_global_action action;
};

static void __print_help(void)
{
    printf("Usage: order [global-options] <command> [command-options]\n");
    printf("\n");
    printf("Commands:\n");
    printf("  account     view account information or setup your account\n");
    printf("  config      view or change configuration values for order\n");
    printf("  package     view or manage your published packages\n");
    printf("  info        retrieves information about a specific pack\n");
    printf("  find        find packages by publisher or by name\n");
    printf("  publish     publish a new pack to chef\n");
    printf("\n");
    printf("Global Options:\n");
    printf("  --root <path>\n");
    printf("      Set a custom root path for all state and data files\n");
    printf("  -h, --help\n");
    printf("      Print this help message\n");
    printf("  -v, --version\n");
    printf("      Print the version of order\n");
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
    struct order_global_options* options = context;

    if (__cli_is_help_switch(argv[*index])) {
        options->action = ORDER_GLOBAL_ACTION_HELP;
        return CLI_PARSE_RESULT_HANDLED;
    }
    if (!strcmp(argv[*index], "-v") || !strcmp(argv[*index], "--version")) {
        options->action = ORDER_GLOBAL_ACTION_VERSION;
        return CLI_PARSE_RESULT_HANDLED;
    }
    if (!strncmp(argv[*index], "--root", 6)) {
        char* root = __split_switch(argv, argc, index);

        if (root == NULL) {
            fprintf(stderr, "order: missing path for --root\n");
            return CLI_PARSE_RESULT_ERROR;
        }
        chef_dirs_set_root(root);
        return CLI_PARSE_RESULT_HANDLED;
    }
    if (argv[*index][0] == '-') {
        fprintf(stderr, "order: invalid global option %s\n", argv[*index]);
        return CLI_PARSE_RESULT_ERROR;
    }
    return CLI_PARSE_RESULT_UNHANDLED;
}

int main(int argc, char** argv, char** envp)
{
    struct command_handler* command = NULL;
    struct order_global_options options = { 0 };
    int                     commandIndex = argc;
    int                     result;

    (void)envp;

    result = __cli_parse_staged_global_options(argc, argv, __parse_global_option, &options, &commandIndex);
    if (result != 0) {
        return -1;
    }

    if (options.action == ORDER_GLOBAL_ACTION_HELP) {
        __print_help();
        return 0;
    }
    if (options.action == ORDER_GLOBAL_ACTION_VERSION) {
        printf("order: version " PROJECT_VER "\n");
        return 0;
    }

    if (commandIndex >= argc) {
        __print_help();
        return 0;
    }

    command = __get_command(argv[commandIndex]);
    if (!command) {
        fprintf(stderr, "order: invalid command %s\n", argv[commandIndex]);
        return -1;
    }

    vlog_initialize(VLOG_LEVEL_DEBUG);
    result = chef_dirs_initialize(CHEF_DIR_SCOPE_BAKE);
    if (result != 0) {
        fprintf(stderr, "order: failed to initialize support library\n");
        return -1;
    }

    result = command->handler(argc - commandIndex, &argv[commandIndex]);
    vlog_cleanup();
    return result;
}
