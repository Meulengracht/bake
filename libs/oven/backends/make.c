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
#include <liboven.h>
#include <stdio.h>

/**
 * @brief Run a Make operation in the requested working directory.
 */
static int __run(struct oven_backend_data* data, const char* arguments, const char* cwd)
{
    struct backend_args args = { 0 };
    int status = -1;

    /* Run the command only when its argument string parsed successfully. */
    if (backend_args_parse(&args, arguments) == 0) {
        status = backend_run(data, "make", &args, cwd, NULL);
    }

    backend_args_destroy(&args);
    return status;
}

static const char* __get_cwd(struct oven_backend_data* data, union chef_backend_options* options)
{
    return (options != NULL && options->make.in_tree) ? data->paths.source : data->paths.build;
}

static int __get_workercount(union chef_backend_options* options)
{
    // never allow the full number of cpucount by default, we always
    // reduce by 2.
    int workers = options != NULL && options->make.parallel > 0
        ? options->make.parallel
        : platform_cpucount() - 2;
    if (workers < 1) {
        // always ensure at least one worker is used
        workers = 1;
    }
    return workers;
}

int make_build_main(struct oven_backend_data* data, union chef_backend_options* options)
{
    struct backend_args args = { 0 };
    const char*         cwd;
    char                jobs[32];
    int                 workers;
    int                 status;

    status = backend_validate(data);
    if (status) {
        return status;
    }

    cwd = __get_cwd(data, options);
    workers = __get_workercount(options);

    // add the job count argument
    snprintf(jobs, sizeof(jobs), "-j%d", workers);
    if (backend_args_add(&args, jobs) != 0 ||
        backend_args_parse(&args, data->arguments) != 0) {
        status = -1;
        goto cleanup;
    }

    // Install only after the compilation command succeeds.
    status = backend_run(data, "make", &args, cwd, NULL);
    if (status == 0) {
        status = __run(data, "install", cwd);
    }

cleanup:
    backend_args_destroy(&args);
    return status;
}

int make_clean_main(struct oven_backend_data* data, union chef_backend_options* options)
{
    const char* cwd;
    int         status;

    status = backend_validate(data);
    if (status) {
        return status;
    }
    return __run(data, "clean", __get_cwd(data, options));
}
