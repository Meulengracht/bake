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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <utils.h>

int make_main(struct oven_backend_data* data)
{
    int    status = -1;
    char** environment;
    char*  argument = NULL;
    size_t argumentLength;

    argumentLength = strlen(data->arguments) + 32;
    argument       = calloc(argumentLength, 1);
    if (argument == NULL) {
        errno = ENOMEM;
        goto cleanup;
    }

    environment = oven_environment_create(data->process_environment, data->environment);
    if (environment == NULL) {
        errno = ENOMEM;
        goto cleanup;
    }

    // build the make parameters, execute from build folder
    sprintf(argument, "-j%i", platform_cpucount());
    if (strlen(data->arguments) > 0) {
        strcat(argument, " ");
        strcat(argument, data->arguments);
    }

    // perform the build operation
    status = platform_spawn("make", argument, (const char* const*)environment, data->build_directory);
    if (status != 0) {
        errno = status;
        fprintf(stderr, "oven-make: failed to execute 'make %s'\n", argument);
        goto cleanup;
    }

    // perform the installation operation, ignore any other parameters
    status = platform_spawn("make", "install", (const char* const*)environment, data->build_directory);
    if (status != 0) {
        errno = status;
        fprintf(stderr, "oven-make: failed to execute 'make install'\n");
    }

cleanup:
    free(argument);
    oven_environment_destroy(environment);
    return status;
}
