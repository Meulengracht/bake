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

int meson_main(struct oven_backend_data* data, union oven_backend_options* options)
{
    char*  mesonCommand = NULL;
    char** environment  = NULL;
    int    status       = -1;

    environment = oven_environment_create(data->process_environment, data->environment);
    if (environment == NULL) {
        errno = ENOMEM;
        return -1;
    }

    mesonCommand = malloc(strlen("meson") + strlen(data->build_directory) + 16);
    if (mesonCommand == NULL) {
        errno = ENOMEM;
        goto cleanup;
    }
    sprintf(mesonCommand, "meson %s", data->build_directory);

    // use the project directory (cwd) as the current build directory
    status = platform_spawn(
        mesonCommand,
        data->arguments,
        (const char* const*)environment,
        NULL
    );

cleanup:
    free(mesonCommand);
    oven_environment_destroy(environment);
    return status;
}
