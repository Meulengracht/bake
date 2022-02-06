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
#include <liboven.h>
#include <libplatform.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

int cmake_main(struct oven_generate_options* options)
{
    char* command;
    char* cwd;
    int   written;
    int   status;

    command = malloc(1024);
    if (command == NULL) {
        errno = ENOMEM;
        return -1;
    }
    memset(command, 0, 1024);
    
    cwd = malloc(1024);
    if (cwd == NULL) {
        free(command);
        errno = ENOMEM;
        return -1;
    }

    status = platform_getcwd(cwd, 1024);
    if (status) {
        goto cleanup;
    }

    status = platform_chdir(".oven/build");
    if (status) {
        goto cleanup;
    }

    // build the cmake command, execute from build folder
    written = snprintf(command, 1023,
        "%s ../..", 
        options->arguments);
    command[written] = '\0';
    status = platform_spawn("cmake", command, NULL);
    if (status) {
        goto cleanup;
    }
    
    // restore working directory
    status = platform_chdir(cwd);
    
cleanup:
    free(cwd);
    free(command);
    return status;
}
