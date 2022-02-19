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

#include <libplatform.h>

#ifdef __linux__

#define _GNU_SOURCE
#include <errno.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

static int __get_arg_count(const char* arguments)
{
    const char* arg   = arguments;
    int         count = 0;
    
    // trim start of arguments
    while (*arg && *arg == ' ') {
        arg++;
    }

    // count the initial argument
    if (*arg) {
        count++;
    }

    while (*arg) {
        // Whitespace denote the end of an argument
        if (*arg == ' ') {
            while (*arg && *arg == ' ') {
                arg++;
            }
            count++;
            continue;
        }

        // We must take quoted arguments into account, even whitespaces
        // in quoted paramters don't mean the end of an argument
        if (*arg == '"') {
            arg++;
            while (*arg && *arg != '"') {
                arg++;
            }
        }
        arg++;
    }
    return count;
}

static void __split_arguments(char* arguments, char** argv)
{
    char* arg = arguments;
    int   i   = 1;

    // set the initial argument, skip whitespaces
    while (*arg && *arg == ' ') {
        arg++;
    }
    argv[i++] = (char*)arg;

    // set the rest of the arguments
    while (*arg) {
        // Whitespace denote the end of an argument
        if (*arg == ' ') {
            // end the argument
            *arg = '\0';
            arg++;
            
            // trim leading spaces
            while (*arg && *arg == ' ') {
                arg++;
            }

            // store the next argument
            argv[i++] = (char*)arg;
            continue;
        }

        // We must take quoted arguments into account, even whitespaces
        // in quoted paramters don't mean the end of an argument
        if (*arg == '"') {
            arg++;
            while (*arg && *arg != '"') {
                arg++;
            }
        }
        arg++;
    }
}

int platform_spawn(const char* path, const char* arguments, const char* const* envp, const char* cwd)
{
    posix_spawn_file_actions_t actions;
    pid_t  pid;
    char** argv;
    int    argc;
    int    status;
    char*  argumentCopy;

    // initialize the argv array
    argc = __get_arg_count(arguments);
    argv = calloc(argc + 2, sizeof(char*));
    if (!argv) {
        return -1;
    }
    
    // first parameter is the executable
    // last parameter is NULL
    argv[0] = (char*)path;
    argv[argc + 1] = NULL;

    // create a copy of the arguments to work on
    argumentCopy = strdup(arguments);
    if (!argumentCopy) {
        free(argv);
        return -1;
    }

    // split the arguments into the argv array
    __split_arguments(argumentCopy, argv);

    // initialize the file actions
    posix_spawn_file_actions_init(&actions);
    if (cwd) {
        // change the working directory
        posix_spawn_file_actions_addchdir_np(&actions, cwd);
    }

    status = posix_spawnp(&pid, path, &actions, NULL, argv, (char* const*)envp);
    posix_spawn_file_actions_destroy(&actions);
    free(argumentCopy);
    free(argv);
    if (status) {
        fprintf(stderr, "platform_spawn: failed to spawn process: %s\n", strerror(errno));
        return -1;
    }

    // wait for the process to complete
    if (waitpid(pid, &status, 0) == -1) {
        return -1;
    }
    return status;
}

#else
#error "spawn: not implemented for this platform"
#endif
