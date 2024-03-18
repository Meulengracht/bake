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

// enable _GNU_SOURCE for chdir on spawn
#define _GNU_SOURCE

#include <errno.h>
#include <chef/platform.h>
#include <poll.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <string.h>

static int __get_arg_count(const char* arguments)
{
    const char* arg   = arguments;
    int         count = 0;

    if (arg == NULL) {
        return 0;
    }
    
    // trim start of arguments
    while (*arg && *arg == ' ') {
        arg++;
    }

    // catch empty strings after trimming
    if (strlen(arg) == 0) {
        return 0;
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

    if (arguments == NULL) {
        return;
    }

    // set the initial argument, skip whitespaces
    while (*arg && *arg == ' ') {
        arg++;
    }

    // catch empty strings after trimming
    if (strlen(arg) == 0) {
        return;
    }

    // set the initial argument
    argv[i++] = (char*)arg;

    // parse the rest of the arguments
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

            // because of how spawn works, we need to strip the quotes
            // from the argument
            argv[i - 1] = (char*)arg; // skip the first quote

            // now skip through the argument
            while (*arg && *arg != '"') {
                arg++;
            }

            // at this point, we need to end the argument, so we replace
            // the closing quote with a null terminator
            if (!(*arg)) {
                break;
            }
            *arg = '\0';
        }
        arg++;
    }
}

// 0 => stdout
// 1 => stderr
int __wait_and_read_stds(struct pollfd* fds, struct platform_spawn_options* options)
{
    char line[2048] = { 0 };

    for (;;) {
        int status = poll(fds, 2, -1);
        if (status <= 0) {
            return status;
        }
        if (fds[0].revents & POLLIN) {
            status = read(fds[0].fd, &line[0], sizeof(line));
            options->output_handler(&line[0], PLATFORM_SPAWN_OUTPUT_TYPE_STDOUT);
        } else if (fds[1].revents & POLLIN) {
            status = read(fds[0].fd, &line[0], sizeof(line));
            options->output_handler(&line[0], PLATFORM_SPAWN_OUTPUT_TYPE_STDERR);
        } else {
            break;
        }
        memset(&line[0], sizeof(line), 0);
    }
    return 0;
}

int platform_spawn(const char* path, const char* arguments, const char* const* envp, struct platform_spawn_options* options)
{
    posix_spawn_file_actions_t actions;
    pid_t                      pid;
    char**                     argv;
    int                        argc;
    int                        status;
    char*                      argumentCopy = NULL;
    int                        outp[2] = { 0 };
    int                        errp[2] = { 0 };

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
    if (arguments) {
        argumentCopy = strdup(arguments);
        if (!argumentCopy) {
            free(argv);
            return -1;
        }
    }

    // split the arguments into the argv array
    __split_arguments(argumentCopy, argv);

    // initialize the file actions
    posix_spawn_file_actions_init(&actions);
    
    if (options && options->cwd) {
        // change the working directory
        posix_spawn_file_actions_addchdir_np(&actions, options->cwd);
    }

    if (options && options->output_handler) {
        // let's redirect and poll for output
        if (pipe(outp) || pipe(errp)) {
            if (outp[0] > 0) {
                close(outp[0]);
                close(outp[1]);
            }
            fprintf(stderr, "platform_spawn: failed to create descriptors: %s\n", strerror(errno));
            return -1;
        }
        posix_spawn_file_actions_addclose(&actions, outp[0]);
        posix_spawn_file_actions_addclose(&actions, errp[0]);
        posix_spawn_file_actions_adddup2(&actions, outp[1], STDOUT_FILENO);
        posix_spawn_file_actions_adddup2(&actions, errp[1], STDERR_FILENO);
        posix_spawn_file_actions_addclose(&actions, outp[1]);
        posix_spawn_file_actions_addclose(&actions, errp[1]);
    }

    // perform the spawn
    status = posix_spawnp(&pid, path, &actions, NULL, argv, (char* const*)envp);
    if (status) {
        fprintf(stderr, "platform_spawn: failed to spawn process: %s\n", strerror(errno));
        goto cleanup;
    }

    if (options && options->output_handler) {
        struct pollfd* fds[2] = { outp[0], outp[1] };

        // close child-side of pipes
        close(outp[1]);
        close(errp[1]); 

        status = __wait_and_read_stds(&fds[0], 2);
        if (status) {
            goto cleanup;
        }
    }

    // wait for the process to complete
    status = waitpid(pid, &argc, 0);
    if (status == 0) {
        status = argc;
    }

cleanup:
    posix_spawn_file_actions_destroy(&actions);
    free(argumentCopy);
    free(argv);
    return status;
}
