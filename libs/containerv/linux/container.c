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

#include "container.h"
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef TEMP_FAILURE_RETRY
#  define TEMP_FAILURE_RETRY(expression)      \
    (__extension__({                          \
      long int __result;                      \
      do                                      \
        __result = (long int) (expression);   \
      while (__result < 0 && errno == EINTR); \
      __result;                               \
    }))
#endif

static inline int __close_safe(int *fd)
{
    int ret = 0;
    if (*fd >= 0) {
        ret = TEMP_FAILURE_RETRY (close (*fd));
        if (ret == 0) {
            *fd = -1;
        }
    }
    return ret;
}


int __detach_process(void)
{
    pid_t pid;

    pid = setsid();
    if (pid < 0) {
        return -1;
    }

    pid = fork ();
    if (pid < 0) {
        return -1;
    }

    if (pid != 0) {
        // skip any CRT cleanup here
        _exit (EXIT_SUCCESS);
    }
    return 0;
}

int __container_run(struct containerv_container* container)
{

}

int __container_entry(int readyfd)
{
    int status;

    // lets start out by detaching process from the current parent,
    // do this by creating a new session and forking again.
    status = __detach_process();
    if (status) {
        return EXIT_FAILURE;
    }

    // This is the primary run function, it initializes the container
    status = __container_run();

    // lets notify the spawner that we now have a result of the container
    // run
    write(readyfd, &status, sizeof (status));
    exit(status == 0 ? EXIT_SUCCESS : EXIT_FAILURE);
}

int __wait_for_container_ready(pid_t child, int readyfd)
{
    int status;
    int exitCode;

    // wait for the detach process to terminate
    // TODO this can return EINTR
    status = waitpid(child, NULL, 0);
    if (status) {
        return -1;
    }

    // read on the ready FD, the status of container spawn will be put there
    // TODO this can return EINTR
    status = (int)read(readyfd, &exitCode, sizeof (exitCode));
    if (status <= 0) {
        return -1;
    }
    return exitCode;
}

int containerv_create(void)
{
    pid_t pid;
    int   readyFds[2];
    int   status;

    status = pipe(&readyFds[0]);
    if (status) {
        return status;
    }

    pid = fork();
    if (pid) {
        return __wait_for_container_ready(pid, readyFds[1]);
    }
    exit(__container_entry(readyFds[0]));
}

int container_exec(void)
{

}

int container_kill(void)
{

}

int container_destroy(void)
{

}
