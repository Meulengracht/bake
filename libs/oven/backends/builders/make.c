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
#include <utils.h>

int make_main(struct oven_backend_data* data)
{
    int    status;
    char** environment;

    environment = oven_environment_create(data->process_environment, data->environment);
    if (environment == NULL) {
        errno = ENOMEM;
        return -1;
    }

    // perform the build operation, supply unmodified args
    printf("oven-make: executing 'make %s'\n", data->arguments);
    status = platform_spawn("make", data->arguments, (const char* const*)environment);
    if (status != 0) {
        printf("oven-make: failed to execute 'make %s'\n", data->arguments);
        goto cleanup;
    }

    // perform the installation operation, supply unmodified args
    printf("oven-make: executing 'make install'\n");
    status = platform_spawn("make", "install", (const char* const*)environment);

cleanup:
    oven_environment_destroy(environment);
    return status;
}
