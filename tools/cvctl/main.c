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
extern int config_main(int argc, char** argv, char** envp, struct cvctl_command_options* options);
extern int uvm_main(int argc, char** argv, char** envp, struct cvctl_command_options* options);

struct command_handler {
    char* name;
    int (*handler)(int argc, char** argv, char** envp, struct cvctl_command_options* options);
};

static struct command_handler g_commands[] = {
    { "start", start_main },
    { "exec",  exec_main },
    { "config", config_main },
    { "uvm", uvm_main }
};

enum cvctl_global_action {
    CVCTL_GLOBAL_ACTION_NONE,
    CVCTL_GLOBAL_ACTION_HELP,
    CVCTL_GLOBAL_ACTION_VERSION
};

struct cvctl_global_options {
    enum cvctl_global_action action;
};

static void __print_help(void)
{
    printf("Usage: cvctl [global-options] <command> [command-options]\n");
    printf("\n");
    printf("Commands:\n");
    printf("  start      starts a new container\n");
    printf("  exec       executes a command inside an existing container\n");
    printf("  config     view or change cvd configuration values\n");
    printf("  uvm        fetch or import LCOW UVM assets\n");
    printf("\n");
    printf("Global Options:\n");
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

static int __parse_global_option(int argc, char** argv, int* index, void* context)
{
    struct cvctl_global_options* options = context;

    if (__cli_is_help_switch(argv[*index])) {
        options->action = CVCTL_GLOBAL_ACTION_HELP;
        return CLI_PARSE_RESULT_HANDLED;
    }
    if (!strcmp(argv[*index], "-v") || !strcmp(argv[*index], "--version")) {
        options->action = CVCTL_GLOBAL_ACTION_VERSION;
        return CLI_PARSE_RESULT_HANDLED;
    }
    if (argv[*index][0] == '-') {
        fprintf(stderr, "cvctl: invalid global option %s\n", argv[*index]);
        return CLI_PARSE_RESULT_ERROR;
    }
    return CLI_PARSE_RESULT_UNHANDLED;
}

int main(int argc, char** argv, char** envp)
{
    struct command_handler*      command = NULL;
    struct cvctl_global_options  globalOptions = { 0 };
    struct cvctl_command_options options = { 0 };
    int                          commandIndex = argc;
    int                          result;

    result = __cli_parse_staged_global_options(argc, argv, __parse_global_option, &globalOptions, &commandIndex);
    if (result != 0) {
        return -1;
    }

    if (globalOptions.action == CVCTL_GLOBAL_ACTION_HELP) {
        __print_help();
        return 0;
    }
    if (globalOptions.action == CVCTL_GLOBAL_ACTION_VERSION) {
        printf("cvctl: version " PROJECT_VER "\n");
        return 0;
    }
    if (commandIndex >= argc) {
        __print_help();
        return 0;
    }

    command = __get_command(argv[commandIndex]);
    if (command == NULL) {
        fprintf(stderr, "cvctl: invalid command %s\n", argv[commandIndex]);
        return -1;
    }
    return command->handler(argc - commandIndex, &argv[commandIndex], envp, &options);
}
