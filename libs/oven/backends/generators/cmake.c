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

#include <backend.h>
#include <errno.h>
#include <liboven.h>
#include <libplatform.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

int cmake_main(struct oven_backend_data* data)
{
    char*  argument;
    char*  cwd;
    int    written;
    int    status;
    size_t argumentLength;

    argumentLength = strlen(data->arguments) + 64;
    argument = malloc(argumentLength);
    if (argument == NULL) {
        errno = ENOMEM;
        return -1;
    }
    memset(argument, 0, argumentLength);
    
    // build the cmake command, execute from build folder
    written = snprintf(argument, argumentLength - 1,
        "%s ../..", 
        data->arguments);
    argument[written] = '\0';
    status = platform_spawn("cmake", argument, data->environment);
    if (status) {
        goto cleanup;
    }
    
cleanup:
    free(argument);
    return status;
}
