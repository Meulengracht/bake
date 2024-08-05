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

#include <chef/containerv.h>
#include <chef/platform.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vlog.h>

#include "commands.h"

static void __print_help(void)
{
    printf("Usage: cvrun exec <socket> <command> [options]\n");
    printf("\n");
    printf("Options:\n");
    printf("  -h, --help\n");
    printf("      Print this help message\n");
}

static void __cleanup_systems(int sig)
{
    (void)sig;
    printf("termination requested, cleaning up\n"); // not safe
    vlog_cleanup();
    _Exit(0);
}

int exec_main(int argc, char** argv, char** envp, struct cvrun_command_options* options)
{
    const char* commSocket = NULL;
    char**      commandArgv = NULL;
    int         commandArgc = 0;
    int         result;

    // catch CTRL-C
    signal(SIGINT, __cleanup_systems);

    // handle individual help command
    if (argc > 2) {
        for (int i = 2; i < argc; i++) {
            if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
                __print_help();
                return 0;
            } else if (argv[i][0] != '-') {
                if (commSocket == NULL) {
                    commSocket = argv[i];
                } else {
                    if (commandArgv == NULL) {
                        // One for NULL termination, one for arg0
                        commandArgv = calloc(argc - i + 2, sizeof(char*));
                        if (commandArgv == NULL) {
                            fprintf(stderr, "cvrun: failed to allocate memory for command\n");
                            return -1;
                        }
                        // Install path as arg0 by doing this twice
                        commandArgv[commandArgc++] = argv[i];
                    }
                    commandArgv[commandArgc++] = argv[i];
                }
            }
        }
    }

    if (commSocket == NULL) {
        fprintf(stderr, "cvrun: no socket path was specified\n");
        __print_help();
        return -1;
    }

    if (commandArgv == NULL) {
        fprintf(stderr, "cvrun: no command was specified\n");
        __print_help();
        return -1;
    }

    // initialize the logging system
    vlog_initialize();
    vlog_set_level(VLOG_LEVEL_DEBUG);
    vlog_add_output(stdout);

    result = containerv_join(commSocket);
    vlog_cleanup();
    if (result) {
        fprintf(stderr, "cvrun: failed to join container at path %s\n", commSocket);
        return -1;
    }

#if __linux__
    result = execve(commandArgv[0], (char* const*)&commandArgv[1], (char* const*)envp);
    if (result) {
        fprintf(stderr, "cvrun: failed to execute %s\n", commandArgv[0]);
    }
#endif
    return 0;
}
