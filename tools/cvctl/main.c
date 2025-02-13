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

#include <chef/containerv.h>
#include <chef/platform.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "chef-config.h"
#include "commands/commands.h"
#include <vlog.h>

static struct containerv_container* g_container = NULL;

extern int start_main(int argc, char** argv, char** envp, struct cvctl_command_options* options);
extern int exec_main(int argc, char** argv, char** envp, struct cvctl_command_options* options);

struct command_handler {
    char* name;
    int (*handler)(int argc, char** argv, char** envp, struct cvctl_command_options* options);
};

static struct command_handler g_commands[] = {
    { "start", start_main },
    { "exec",  exec_main }
};

static void __print_help(void)
{
    printf("Usage: cvctl <command> [options]\n");
    printf("\n");
    printf("Commands:\n");
    printf("  start      starts a new container\n");
    printf("  exec       executes a command inside an existing container\n");
    printf("\n");
    printf("Options:\n");
    printf("  -h, --help\n");
    printf("      Print this help message\n");
    printf("  -v, --version\n");
    printf("      Print the version of cvctl\n");
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
    struct command_handler*      command = NULL;
    struct cvctl_command_options options = { 0 };

    // first argument must be the command if not --help or --version
    if (argc > 1) {
        if (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")) {
            __print_help();
            return 0;
        }

        if (!strcmp(argv[1], "-v") || !strcmp(argv[1], "--version")) {
            printf("cvctl: version " PROJECT_VER "\n");
            return 0;
        }

        command = __get_command(argv[1]);
    }

    if (command == NULL) {
        fprintf(stderr, "cvctl: invalid command %s\n", argv[1]);
        return -1;
    }
    return command->handler(argc, argv, envp, &options);
}
