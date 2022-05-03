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
#include <chef/platform.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <utils.h>

static const char* g_siteTemplate = 
"# This file is generated by chef please don't edit it.\n"
"# The arguments listed there are the one used by the last generation of this file.\n"
"#\n"
"# Please take a look at what we're doing there if you are curious!\n"
"# If you have feedback or improvements, please open an issue at our github tracker:\n"
"#\n"
"#    https://github.com/meulengracht/bake/issues\n"
"#\n";

/*
$ mkdir -p ~/local/share
$ cat << EOF > ~/local/share/config.site
CPPFLAGS=-I$HOME/local/include
LDFLAGS=-L$HOME/local/lib
...
EOF
*/

static int __generate_site_file(const char* path, struct oven_backend_data* data)
{
    FILE* file;

    file = fopen(path, "w");
    if(!file) {
        fprintf(stderr, "oven-configure: failed to open %s for writing: %s\n", path, strerror(errno));
        return -1;
    }

    fwrite(g_siteTemplate, strlen(g_siteTemplate), 1, file);
    fprintf(file, "CFLAGS=-I%s/include\n", data->fridge_directory);
    fprintf(file, "CPPFLAGS=-I%s/include\n", data->fridge_directory);
    fprintf(file, "LDFLAGS=-L%s/lib\n", data->fridge_directory);
    fclose(file);
    return 0;
}

int configure_main(struct oven_backend_data* data, union oven_backend_options* options)
{
    char*  configSitePath;
    char*  argument      = NULL;
    char*  sharePath     = NULL;
    char*  configurePath = NULL;
    char** environment   = NULL;
    int    status = -1;
    int    written;
    size_t argumentLength;

    sharePath      = strpathcombine(data->install_directory, "share");
    configSitePath = strpathcombine(sharePath, "config.site");
    configurePath  = strpathcombine(data->project_directory, "configure");
    if (sharePath == NULL || configSitePath == NULL || configurePath == NULL) {
        free(sharePath);
        free(configSitePath);
        free(configurePath);
        return -1;
    }

    // create the share directory
    if (platform_mkdir(sharePath)) {
        fprintf(stderr, "oven-configure: failed to create %s: %s\n", sharePath, strerror(errno));
        goto cleanup;
    }

    argumentLength = strlen(data->arguments) + 1024;
    argument       = malloc(argumentLength);
    if (argument == NULL) {
        goto cleanup;
    }

    environment = oven_environment_create(data->process_environment, data->environment);
    if (environment == NULL) {
        free(argument);
        goto cleanup;
    }

    status = __generate_site_file(configSitePath, data);
    if (status != 0) {
        goto cleanup;
    }

    // build the cmake command, execute from build folder
    // if cross compiling set -DCMAKE_FIND_USE_CMAKE_SYSTEM_PATH=OFF
    written = snprintf(
        argument,
        argumentLength - 1,
        "%s --prefix=%s",
        data->arguments,
        data->install_directory
    );
    argument[written] = '\0';

    // perform the spawn operation
    printf("oven-configure: executing '%s %s'\n", configurePath, argument);
    status = platform_spawn(configurePath, argument, (const char* const*)environment, data->build_directory);
    
cleanup:
    oven_environment_destroy(environment);
    free(argument);
    free(sharePath);
    free(configSitePath);
    free(configurePath);
    return status;
}
