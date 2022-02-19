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
#include <utils.h>

int cmake_main(struct oven_backend_data* data)
{
    char*  argument;
    char*  cwd;
    char** environment;
    int    written;
    int    status;
    size_t argumentLength;

    argumentLength = strlen(data->arguments) + strlen(data->install_directory) + 64;
    argument       = malloc(argumentLength);
    if (argument == NULL) {
        errno = ENOMEM;
        return -1;
    }

    environment = oven_environment_create(data->process_environment, data->environment);
    if (environment == NULL) {
        free(argument);
        errno = ENOMEM;
        return -1;
    }

    // build the cmake command, execute from build folder
    written = snprintf(
        argument, 
        argumentLength - 1,
        "%s -DCMAKE_INSTALL_PREFIX=%s ../..", 
        data->arguments,
        data->install_directory
    );
    argument[written] = '\0';

    // perform the spawn operation
    printf("oven-cmake: executing 'cmake %s'\n", argument);
    status = platform_spawn("cmake", argument, (const char* const*)environment, data->build_directory);
    
    oven_environment_destroy(environment);
    free(argument);
    return status;
}
