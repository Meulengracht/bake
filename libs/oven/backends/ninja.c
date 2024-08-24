/**
 * Copyright 2024, Philip Meulengracht
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vlog.h>

#define __INTERNAL_MAX(a,b) (((a) > (b)) ? (a) : (b))

static void __ninja_output_handler(const char* line, enum platform_spawn_output_type type) 
{
    if (type == PLATFORM_SPAWN_OUTPUT_TYPE_STDOUT) {
        VLOG_TRACE("ninja", line);

        // re-enable again if it continues to print
        vlog_set_output_options(stdout, VLOG_OUTPUT_OPTION_RETRACE);
    } else {
        // clear retrace on error output
        vlog_clear_output_options(stdout, VLOG_OUTPUT_OPTION_RETRACE);
        
        VLOG_ERROR("ninja", line);
    }
}

int ninja_build_main(struct oven_backend_data* data, union chef_backend_options* options)
{
    int    status      = -1;
    char** environment = NULL;

    environment = environment_create(data->process_environment, data->environment);
    if (environment == NULL) {
        goto cleanup;
    }

    // perform the build operation
    VLOG_TRACE("ninja", "executing 'ninja %s'\n", data->arguments);
    vlog_set_output_options(stdout, VLOG_OUTPUT_OPTION_RETRACE | VLOG_OUTPUT_OPTION_NODECO);
    status = platform_spawn(
        "ninja",
        data->arguments,
        (const char* const*)environment, 
        &(struct platform_spawn_options) {
            .cwd = data->paths.build,
            .output_handler = __ninja_output_handler
        }
    );
    vlog_clear_output_options(stdout, VLOG_OUTPUT_OPTION_RETRACE | VLOG_OUTPUT_OPTION_NODECO);
    if (status != 0) {
        VLOG_ERROR("ninja", "failed to execute 'ninja %s'\n", data->arguments);
        goto cleanup;
    }

    // perform the installation operation, ignore any other parameters
    VLOG_TRACE("ninja", "executing 'ninja install'\n");
    vlog_set_output_options(stdout, VLOG_OUTPUT_OPTION_RETRACE | VLOG_OUTPUT_OPTION_NODECO);
    status = platform_spawn(
        "ninja",
        "install",
        (const char* const*)environment, 
        &(struct platform_spawn_options) {
            .cwd = data->paths.build,
            .output_handler = __ninja_output_handler
        }
    );
    vlog_clear_output_options(stdout, VLOG_OUTPUT_OPTION_RETRACE | VLOG_OUTPUT_OPTION_NODECO);
    if (status != 0) {
        VLOG_ERROR("ninja", "failed to execute 'ninja install'\n");
    }

cleanup:
    environment_destroy(environment);
    return status;
}

int ninja_clean_main(struct oven_backend_data* data, union chef_backend_options* options)
{
    int    status      = -1;
    char** environment = NULL;

    environment = environment_create(data->process_environment, data->environment);
    if (environment == NULL) {
        goto cleanup;
    }

    // perform the build operation
    VLOG_TRACE("ninja", "executing 'ninja clean'\n");
    vlog_set_output_options(stdout, VLOG_OUTPUT_OPTION_RETRACE | VLOG_OUTPUT_OPTION_NODECO);
    status = platform_spawn(
        "ninja",
        "clean",
        (const char* const*)environment, 
        &(struct platform_spawn_options) {
            .cwd = data->paths.build,
            .output_handler = __ninja_output_handler
        }
    );
    vlog_clear_output_options(stdout, VLOG_OUTPUT_OPTION_RETRACE | VLOG_OUTPUT_OPTION_NODECO);
    if (status != 0) {
        VLOG_ERROR("ninja", "failed to execute 'ninja clean'\n");
        goto cleanup;
    }

cleanup:
    environment_destroy(environment);
    return status;
}
