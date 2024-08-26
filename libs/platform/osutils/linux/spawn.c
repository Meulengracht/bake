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

static void __report(char* line, enum platform_spawn_output_type type, struct platform_spawn_options* options)
{
    const char* s = line;
    char*       p = line;
    char        tmp[2048];

    while (*p) {
        if (*p == '\n') {
            // include the \n
            size_t count = (size_t)(p - s) + 1;
            strncpy(&tmp[0], s, count);

            // zero terminate the string and report
            tmp[count] = '\0';
            options->output_handler(&tmp[0], type);

            // update new start
            s = ++p;
        } else {
            p++;
        }
    }
    
    // only do a final report if the line didn't end with a newline
    if (s != p) {
        options->output_handler(s, type);
    }
}

// 0 => stdout
// 1 => stderr
static void __wait_and_read_stds(struct pollfd* fds, struct platform_spawn_options* options)
{
    char line[2048];

    for (;;) {
        int status = poll(fds, 2, -1);
        if (status <= 0) {
            return;
        }
        if (fds[0].revents & POLLIN) {
            status = read(fds[0].fd, &line[0], sizeof(line));
            line[status] = 0;
            __report(&line[0], PLATFORM_SPAWN_OUTPUT_TYPE_STDOUT, options);
        } else if (fds[1].revents & POLLIN) {
            status = read(fds[1].fd, &line[0], sizeof(line));
            line[status] = 0;
            __report(&line[0], PLATFORM_SPAWN_OUTPUT_TYPE_STDERR, options);
        } else {
            break;
        }
    }
}

int platform_spawn(const char* path, const char* arguments, const char* const* envp, struct platform_spawn_options* options)
{
    posix_spawn_file_actions_t actions;
    pid_t                      pid;
    char**                     argv;
    int                        status;
    char*                      argumentCopy = NULL;
    int                        outp[2] = { 0 };
    int                        errp[2] = { 0 };

    // create a copy of the arguments to work on
    if (arguments) {
        argumentCopy = strdup(arguments);
        if (!argumentCopy) {
            return -1;
        }
    }

    argv = strargv(argumentCopy, (options && options->argv0) ? options->argv0 : path, NULL);
    if (argv == NULL) {
        free(argumentCopy);
        return -1;
    }

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
            goto cleanup;
        }
        posix_spawn_file_actions_adddup2(&actions, outp[1], STDOUT_FILENO);
        posix_spawn_file_actions_adddup2(&actions, errp[1], STDERR_FILENO);
    }

    // perform the spawn
    status = posix_spawnp(&pid, path, &actions, NULL, argv, (char* const*)envp);
    if (status) {
        fprintf(stderr, "platform_spawn: failed to spawn process: %s\n", strerror(errno));
        goto cleanup;
    }

    if (options && options->output_handler) {
        struct pollfd fds[2] = { 
            { 
                .fd = outp[0],
                .events = POLLIN
            },
            {
                .fd = errp[0],
                .events = POLLIN
            }
        };

        // close child-side of pipes
        close(outp[1]);
        close(errp[1]); 

        __wait_and_read_stds(&fds[0], options);
    }

    // wait for the process to complete
    waitpid(pid, &status, 0);

cleanup:
    posix_spawn_file_actions_destroy(&actions);
    free(argumentCopy);
    strargv_free(argv);
    return status;
}
