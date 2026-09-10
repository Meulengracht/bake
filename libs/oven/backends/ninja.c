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

#include "private.h"

static int __run(struct oven_backend_data* data, const char* arguments)
{
    struct backend_args args = { 0 };
    int status = -1;

    if (backend_args_parse(&args, arguments) == 0) {
        status = backend_run(data, "ninja", &args, data->paths.build, NULL);
    }

    backend_args_destroy(&args);
    return status;
}

int ninja_build_main(struct oven_backend_data* data, union chef_backend_options* options)
{
    int status;
    (void)options;

    status = backend_validate(data);
    if (status) {
        return status;
    }
    
    // build then install
    status = __run(data, data->arguments);
    if (status) {
        return status;
    }
    return __run(data, "install");
}

int ninja_clean_main(struct oven_backend_data* data, union chef_backend_options* options)
{
    int status;
    (void)options;

    status = backend_validate(data);
    if (status) {
        return status;
    }
    return __run(data, "clean");
}
