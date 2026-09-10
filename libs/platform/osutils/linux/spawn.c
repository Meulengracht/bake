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
    int  openCount = 2;

    while (openCount > 0) {
        int status = poll(fds, 2, -1);
        if (status < 0 && errno == EINTR) {
            // retry on EINTR
            continue;
        }

        if (status <= 0) {
            // poll returned 0 or an error other than EINTR
            // then we abort
            return;
        }

        for (int i = 0; i < 2; i++) {
            ssize_t size;

            // check for events
            if (!(fds[i].revents & (POLLIN | POLLHUP | POLLERR | POLLNVAL))) {
                continue;
            }

            size = read(fds[i].fd, line, sizeof(line) - 1);
            if (size < 0 && errno == EINTR) {
                // only for EINTR will we retry
                continue;
            }

            // handle eof and read errors
            if (size <= 0) {
                fds[i].fd = -1;
                openCount--;
                continue;
            }

            line[size] = '\0';
            __report(
                line,
                i == 0 ? PLATFORM_SPAWN_OUTPUT_TYPE_STDOUT
                       : PLATFORM_SPAWN_OUTPUT_TYPE_STDERR, 
                options
            );
        }
    }
}

int platform_spawn_argv(const char* path, const char* const* arguments,
    const char* const* envp, struct platform_spawn_options* options)
{
    posix_spawn_file_actions_t actions;
    pid_t                      pid;
    char**                     argv;
    int                        status = -1;
    int                        outp[2] = { -1, -1 };
    int                        errp[2] = { -1, -1 };
    size_t count = 0;

    if (path == NULL) {
        errno = EINVAL;
        return -1;
    }

    // get the actual argument count
    while (arguments != NULL && arguments[count] != NULL) {
        count++;
    }

    argv = calloc(count + 2, sizeof(char*));
    if (argv == NULL) {
        return -1;
    }

    // it's important to note here we don't create argv copies, but
    // rather maintain the ownership for the caller
    argv[0] = (char*)((options != NULL && options->argv0 != NULL)
        ? options->argv0
        : path);
    for (size_t i = 0; i < count; i++) {
        argv[i + 1] = (char*)arguments[i];
    }

    // initialize the file actions
    // posix_spawn* reports its error directly rather than through errno
    status = posix_spawn_file_actions_init(&actions);
    if (status) {
        free(argv);
        errno = status;
        return -1;
    }

    status = -1;

    if (options != NULL && options->cwd != NULL) {
        // change the working directory
        int error = posix_spawn_file_actions_addchdir_np(&actions, options->cwd);
        if (error != 0) {
            errno = error;
            goto cleanup;
        }
    }

    if (options != NULL && options->output_handler != NULL) {
        // let's redirect and poll for output
        if (pipe(outp) != 0 || pipe(errp) != 0) {
            fprintf(stderr, "platform_spawn: failed to create descriptors: %s\n", strerror(errno));
            goto cleanup;
        }

        int error = posix_spawn_file_actions_adddup2(&actions, outp[1], STDOUT_FILENO);
        // Redirect stdout first, then stderr, so both streams use the same setup.
        if (error == 0) {
            error = posix_spawn_file_actions_adddup2(&actions, errp[1], STDERR_FILENO);
        }

        // Close every pipe end the child does not need after duplication.
        for (int i = 0; error == 0 && i < 2; i++) {
            // The child keeps only the standard descriptors after duplication.
            if (outp[i] != STDOUT_FILENO && outp[i] != STDERR_FILENO) {
                error = posix_spawn_file_actions_addclose(&actions, outp[i]);
            }

            // Do not add a second close action after the first one fails.
            if (error == 0 && errp[i] != STDOUT_FILENO && errp[i] != STDERR_FILENO) {
                error = posix_spawn_file_actions_addclose(&actions, errp[i]);
            }
        }

        if (error) {
            errno = error;
            goto cleanup;
        }
    }

    // perform the spawn
    status = posix_spawnp(&pid, path, &actions, NULL, argv, (char* const*)envp);
    if (status) {
        errno = status;
        fprintf(stderr, "platform_spawn: failed to spawn process %s: %s\n", path, strerror(errno));
        goto cleanup;
    }

    if (options != NULL && options->output_handler != NULL) {
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
        outp[1] = errp[1] = -1;

        __wait_and_read_stds(&fds[0], options);
    }

    // wait for the process to complete
    while (waitpid(pid, &status, 0) < 0) {
        // We can recover from EINTR
        if (errno != EINTR) {
            status = -1;
            break;
        }
    }

cleanup:
    for (int i = 0; i < 2; i++) {
        if (outp[i] >= 0) {
            close(outp[i]);
        }

        if (errp[i] >= 0) {
            close(errp[i]);
        }
    }

    posix_spawn_file_actions_destroy(&actions);
    strargv_free(argv);
    return status;
}

int platform_spawn(const char* path, const char* arguments, const char* const* envp,
    struct platform_spawn_options* options)
{
    char* copy;
    char** argv;
    int status;

    copy = arguments != NULL ? platform_strdup(arguments) : NULL;
    if (arguments != NULL && copy == NULL) {
        return -1;
    }

    argv = strargv(copy, NULL, NULL);
    if (argv == NULL) {
        free(copy);
        return -1;
    }

    status = platform_spawn_argv(path, (const char* const*)argv, envp, options);
    strargv_free(argv);
    free(copy);
    return status;
}
