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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "chef-config.h"

extern int account_main(int argc, char** argv);
extern int info_main(int argc, char** argv);
extern int publish_main(int argc, char** argv);

struct command_handler {
    char* name;
    int (*handler)(int argc, char** argv);
};

static struct command_handler g_commands[] = {
    { "account",  account_main },
    { "info",     info_main },
    { "publish",  publish_main }
};

static void __print_help(void)
{
    printf("Usage: order <command> [options]\n");
    printf("\n");
    printf("Commands:\n");
    printf("  account     view account information or setup your account\n");
    printf("  info        retrieves information about a specific pack\n");
    printf("  publish     publish a new pack to chef\n");
    printf("\n");
    printf("Options:\n");
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

int main(int argc, char** argv, char** envp)
{
    struct command_handler* command = NULL;
    int                     result;

    // first argument must be the command if not --help or --version
    if (argc > 1) {
        if (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")) {
            __print_help();
            return 0;
        }

        if (!strcmp(argv[1], "-v") || !strcmp(argv[1], "--version")) {
            printf("order: version " PROJECT_VER "\n");
            return 0;
        }

        command = __get_command(argv[1]);
        if (!command) {
            fprintf(stderr, "order: invalid command %s\n", argv[1]);
            return -1;
        }
    }

    if (!command) {
        __print_help();
        return 0;
    }

    result = command->handler(argc, argv);
    if (result != 0) {
        return result;
    }
    return 0;
}
