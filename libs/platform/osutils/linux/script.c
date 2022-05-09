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

#include <errno.h>
#include <chef/platform.h>
#include <stdio.h>
#include <stdlib.h>
#include <spawn.h>
#include <string.h>
#include <sys/wait.h>

int platform_script(const char* script)
{
    pid_t  pid;
    char** argv;
    int    argc;
    int    status;

    // initialize the argv array
    argv = calloc(4, sizeof(char*));
    if (!argv) {
        return -1;
    }
    
    // first parameter is the executable
    // last parameter is NULL
    argv[0] = "/bin/sh";
    argv[1] = "-c";
    argv[2] = (char*)script;
    argv[3] = NULL;

    status = posix_spawnp(&pid, "/bin/sh", NULL, NULL, argv, NULL);
    free(argv);
    if (status) {
        fprintf(stderr, "platform_script: failed to spawn process: %s\n", strerror(errno));
        return -1;
    }

    // wait for the process to complete
    if (waitpid(pid, &status, 0) == -1) {
        return -1;
    }
    return status;
}
