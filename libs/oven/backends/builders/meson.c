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
#include <chef/environment.h>
#include <chef/platform.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

int meson_build_main(struct oven_backend_data* data, union chef_backend_options* options)
{
    char*  mesonCommand = NULL;
    char** environment  = NULL;
    int    status = -1;
    size_t length;

    environment = environment_create(data->process_environment, data->environment);
    if (environment == NULL) {
        errno = ENOMEM;
        return -1;
    }

    // lets make it 64 to cover some extra grounds
    length = 64 + strlen(data->paths.project);

    mesonCommand = malloc(length);
    if (mesonCommand == NULL) {
        errno = ENOMEM;
        goto cleanup;
    }

    sprintf(mesonCommand, "compile -C %s", data->paths.project);
    
    // use the project directory (cwd) as the current build directory
    status = platform_spawn(
        "meson",
        data->arguments,
        (const char* const*)environment,
        &(struct platform_spawn_options) {
            .cwd = data->paths.build
        }
    );

cleanup:
    free(mesonCommand);
    environment_destroy(environment);
    return status;
}
