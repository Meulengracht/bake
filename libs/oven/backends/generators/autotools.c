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
#include <vlog.h>

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

// TODO this exists in both autotools and cmake backend, move to a common
// directory?
static const char* __get_default_install_path(const char* platform)
{
    if (strcmp(platform, "windows") == 0) {
        // TODO is windows really necessary to handle?
        return "Program Files";
    } else if (strcmp(platform, "linux") == 0) {
        return "/usr/local";
    } else if (strcmp(platform, "vali") == 0) {
        return "";
    } else {
        return "";
    }
}

static char* __add_prefix(const char* platform, const char* arguments, const char* installPath, char** prefixPathOut)
{
    // build a new argument string with the prefix added
    char* newArguments = malloc(strlen(arguments) + 1024);
    char* defaultInstallPath = strpathcombine(installPath, __get_default_install_path(platform));
    if (newArguments == NULL || defaultInstallPath == NULL) {
        free(newArguments);
        free(defaultInstallPath);
        return NULL;
    }

    sprintf(newArguments, "%s --prefix=%s", arguments, defaultInstallPath);
    
    *prefixPathOut = defaultInstallPath;
    return newArguments;
}

static char* __replace_or_add_prefix(const char* platform, const char* arguments, const char* installPath, char** prefixPathOut)
{
    char* prefix = strstr(arguments, "--prefix=");
    char* newArguments;
    char* oldPath;
    char* newPath;
    char* startOfValue;
    char* endOfValue;

    if (!prefix) {
        return __add_prefix(platform, arguments, installPath, prefixPathOut);
    }

    // replace the prefix with the new one
    startOfValue = prefix + 9; // '--prefix=' is 9 characters long
    endOfValue   = strchr(startOfValue, ' ');
    if (!endOfValue) {
        endOfValue = strchr(startOfValue, '\0');
    }

    oldPath      = platform_strndup(startOfValue, endOfValue - startOfValue);
    newArguments = malloc(strlen(arguments) + 1024);
    if (oldPath == NULL|| newArguments == NULL) {
        free(oldPath);
        free(newArguments);
        return NULL;
    }

    // build the new path which is a combination of oldPath and install directory
    newPath = strpathcombine(installPath, oldPath);
    free(oldPath);
    
    if (newPath == NULL) {
        free(newArguments);
        return NULL;
    }

    strncpy(newArguments, arguments, prefix - arguments);
    strcat(newArguments, "--prefix=");
    strcat(newArguments, newPath);
    if (*endOfValue) {
        strcat(newArguments, endOfValue);
    }

    *prefixPathOut = newPath;
    return newArguments;
}

static int __generate_site_file(const char* path, struct oven_backend_data* data)
{
    FILE* file;

    file = fopen(path, "w");
    if(!file) {
        VLOG_ERROR("configure", "failed to open %s for writing: %s\n", path, strerror(errno));
        return -1;
    }

    fwrite(g_siteTemplate, strlen(g_siteTemplate), 1, file);
    fprintf(file, "CFLAGS=-I%s/include -I%s/usr/include -I%s/usr/local/include\n",
        "data->paths.ingredients", "data->paths.ingredients", "data->paths.ingredients");
    fprintf(file, "CPPFLAGS=-I%s/include -I%s/usr/include -I%s/usr/local/include\n",
        "data->paths.ingredients", "data->paths.ingredients", "data->paths.ingredients");
    fprintf(file, "LDFLAGS=-L%s/lib -L%s/usr/lib -L%s/usr/local/lib\n",
        "data->paths.ingredients", "data->paths.ingredients", "data->paths.ingredients");
    fclose(file);
    return 0;
}

int configure_main(struct oven_backend_data* data, union oven_backend_options* options)
{
    char*  configSitePath;
    char*  arguments     = NULL;
    char*  installPath   = NULL;
    char*  sharePath     = NULL;
    char*  configurePath = NULL;
    char** environment   = NULL;
    int    status = -1;
    int    written;

    arguments = __replace_or_add_prefix(
        data->platform.target_platform,
        data->arguments,
        data->paths.install,
        &installPath
    );
    if (arguments == NULL) {
        return -1;
    }

    sharePath      = strpathcombine(installPath, "share");
    configSitePath = strpathcombine(sharePath, "config.site");
    configurePath  = strpathcombine(data->paths.project, "configure");
    if (sharePath == NULL || configSitePath == NULL || configurePath == NULL) {
        free(arguments);
        free(sharePath);
        free(configSitePath);
        free(configurePath);
        return -1;
    }

    // create the share directory
    if (platform_mkdir(sharePath)) {
        VLOG_ERROR("configure", "failed to create %s: %s\n", sharePath, strerror(errno));
        goto cleanup;
    }

    environment = environment_create(data->process_environment, data->environment);
    if (environment == NULL) {
        goto cleanup;
    }

    status = __generate_site_file(configSitePath, data);
    if (status != 0) {
        goto cleanup;
    }

    // perform the spawn operation
    VLOG_TRACE("configure", "executing '%s %s'\n", configurePath, arguments);
    status = platform_spawn(
        configurePath,
        arguments,
        (const char* const*)environment,
        &(struct platform_spawn_options) {
            .cwd = data->paths.build
        }
    );
    
cleanup:
    environment_destroy(environment);
    free(arguments);
    free(sharePath);
    free(configSitePath);
    free(configurePath);
    return status;
}
