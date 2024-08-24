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
#include <chef/environment.h>
#include <chef/platform.h>
#include <errno.h>
#include <liboven.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vlog.h>

#define __INTERNAL_MAX(a,b) (((a) > (b)) ? (a) : (b))

static void __ninja_output_handler(const char* line, enum platform_spawn_output_type type) 
{
    if (type == PLATFORM_SPAWN_OUTPUT_TYPE_STDOUT) {
        VLOG_TRACE("make", line);

        // re-enable again if it continues to print
        vlog_set_output_options(stdout, VLOG_OUTPUT_OPTION_RETRACE);
    } else {
        // clear retrace on error output
        vlog_clear_output_options(stdout, VLOG_OUTPUT_OPTION_RETRACE);
        
        VLOG_ERROR("make", line);
    }
}

static int __cpu_workers(union chef_backend_options* options)
{
    if (options->make.parallel > 0) {
        return options->make.parallel;
    }
    // Never use the maximum number of cpus, that can make a system unstable/hang
    return __INTERNAL_MAX(platform_cpucount() - 2, 1);
}

int make_build_main(struct oven_backend_data* data, union chef_backend_options* options)
{
    int         status      = -1;
    char**      environment = NULL;
    char*       argument    = NULL;
    size_t      argumentLength;
    const char* cwd = data->paths.build;

    argumentLength = strlen(data->arguments) + 32;
    argument       = calloc(argumentLength, 1);
    if (argument == NULL) {
        return -1;
    }

    environment = environment_create(data->process_environment, data->environment);
    if (environment == NULL) {
        goto cleanup;
    }

    // build the make parameters, execute from build folder
    sprintf(argument, "-j%i", __cpu_workers(options));
    if (strlen(data->arguments) > 0) {
        strcat(argument, " ");
        strcat(argument, data->arguments);
    }

    // handle in-tree builds
    if (options->make.in_tree) {
        cwd = data->paths.source;
    }

    // perform the build operation
    VLOG_TRACE("make", "executing 'make %s'\n", argument);
    vlog_set_output_options(stdout, VLOG_OUTPUT_OPTION_RETRACE | VLOG_OUTPUT_OPTION_NODECO);
    status = platform_spawn(
        "make",
        argument,
        (const char* const*)environment, 
        &(struct platform_spawn_options) {
            .cwd = cwd,
            .output_handler = __ninja_output_handler
        }
    );
    vlog_clear_output_options(stdout, VLOG_OUTPUT_OPTION_RETRACE | VLOG_OUTPUT_OPTION_NODECO);
    if (status != 0) {
        VLOG_ERROR("make", "failed to execute 'make %s'\n", argument);
        goto cleanup;
    }

    // perform the installation operation, ignore any other parameters
    VLOG_TRACE("make", "executing 'make install'\n");
    vlog_set_output_options(stdout, VLOG_OUTPUT_OPTION_RETRACE | VLOG_OUTPUT_OPTION_NODECO);
    status = platform_spawn(
        "make",
        "install",
        (const char* const*)environment, 
        &(struct platform_spawn_options) {
            .cwd = cwd,
            .output_handler = __ninja_output_handler
        }
    );
    vlog_clear_output_options(stdout, VLOG_OUTPUT_OPTION_RETRACE | VLOG_OUTPUT_OPTION_NODECO);
    if (status != 0) {
        VLOG_ERROR("make", "failed to execute 'make install'\n");
    }

cleanup:
    free(argument);
    environment_destroy(environment);
    return status;
}


int make_clean_main(struct oven_backend_data* data, union chef_backend_options* options)
{
    int         status      = -1;
    char**      environment = NULL;
    const char* cwd = data->paths.build;

    environment = environment_create(data->process_environment, data->environment);
    if (environment == NULL) {
        goto cleanup;
    }

    // handle in-tree builds
    if (options->make.in_tree) {
        cwd = data->paths.source;
    }

    // perform the installation operation, ignore any other parameters
    VLOG_TRACE("make", "executing 'make clean'\n");
    vlog_set_output_options(stdout, VLOG_OUTPUT_OPTION_RETRACE | VLOG_OUTPUT_OPTION_NODECO);
    status = platform_spawn(
        "make",
        "clean",
        (const char* const*)environment, 
        &(struct platform_spawn_options) {
            .cwd = cwd,
            .output_handler = __ninja_output_handler
        }
    );
    vlog_clear_output_options(stdout, VLOG_OUTPUT_OPTION_RETRACE | VLOG_OUTPUT_OPTION_NODECO);
    if (status != 0) {
        VLOG_ERROR("make", "failed to execute 'make clean'\n");
    }

cleanup:
    environment_destroy(environment);
    return status;
}
