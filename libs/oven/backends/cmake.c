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
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <vlog.h>

#if CHEF_ON_WINDOWS
static const char* g_pathSeperator = ";";
#else
static const char* g_pathSeperator = ":";
#endif

static const char* g_cmakeTemplate = 
"# This file is generated by chef please don't edit it.\n"
"# The arguments listed there are the one used by the last generation of this file.\n"
"#\n"
"# Please take a look at what we're doing there if you are curious!\n"
"# If you have feedback or improvements, please open an issue at our github tracker:\n"
"#\n"
"#    https://github.com/meulengracht/bake/issues\n"
"#\n"
"set(CHEF_CMAKE_{{PROJECT_NAME}} ON)\n"
"set(CHEF_CMAKE ON)\n"
"set(CHEF_CMAKE_{{PROJECT_NAME}}_PROFILE \"{{PROFILE_NAME}}\")\n"
"set(CHEF_CMAKE_PROFILE \"{{PROFILE_NAME}}\")\n";

char* __replace(char* text, const char* find, const char* replaceWith)
{
    char* result = strreplace(text, find, replaceWith);
    if (result == NULL) {
        return NULL;
    }
    free(text);
    return result;
}

static int __write_header(FILE* file, const char* projectName, const char* profileName)
{
    char* cmake;

    cmake = platform_strdup(g_cmakeTemplate);
    if (cmake == NULL) {
        errno = ENOMEM;
        return -1;
    }
    
    cmake = __replace(cmake, "{{PROJECT_NAME}}", projectName);
    cmake = __replace(cmake, "{{PROFILE_NAME}}", profileName);

    fwrite(cmake, strlen(cmake), 1, file);
    free(cmake);
    return 0;
}

static void __write_linux_prefix(FILE* file, const char* prefixPath)
{
    fprintf(file, "\n# setup the linux environment paths\n");
    fprintf(file, "list(APPEND CMAKE_PREFIX_PATH \"%s\")\n", prefixPath);
    fprintf(file, "list(APPEND CMAKE_PREFIX_PATH \"%s/usr\")\n", prefixPath);
    fprintf(file, "list(APPEND CMAKE_PREFIX_PATH \"%s/usr/local\")\n", prefixPath);
}

static void __write_linux_include(FILE* file, const char* includePath)
{
    fprintf(file, "\n# setup additional include paths for linux code environment\n");
    fprintf(file, "include_directories(\"%s/include\")\n", includePath);
    fprintf(file, "include_directories(\"%s/usr/include\")\n", includePath);
    fprintf(file, "include_directories(\"%s/usr/local/include\")\n", includePath);
}

static void __write_windows_prefix(FILE* file, const char* prefixPath)
{
    fprintf(file, "\n# setup the windows environment paths\n");
    fprintf(file, "list(APPEND CMAKE_PREFIX_PATH \"%s\")\n", prefixPath);
    fprintf(file, "list(APPEND CMAKE_PREFIX_PATH \"%s/Program Files\")\n", prefixPath);
}

static void __write_windows_include(FILE* file, const char* includePath)
{
    fprintf(file, "\n# setup additional include paths for windows code environment\n");
    fprintf(file, "include_directories(\"%s/Program Files/include\")\n", includePath);
}

static void __write_default_prefix(FILE* file, const char* prefixPath)
{
    fprintf(file, "\n# setup the default environment path\n");
    fprintf(file, "list(APPEND CMAKE_PREFIX_PATH \"%s\")\n", prefixPath);
}

static void __write_default_include(FILE* file, const char* includePath)
{
    fprintf(file, "\n# setup additional include paths for code\n");

    // Issue: these do not work, they need to appear after project()
    //fprintf(file, "list(APPEND CMAKE_INCLUDE_PATH \"%s/include\")\n", includePath);
    //fprintf(file, "include_directories(AFTER SYSTEM \"%s/include\")\n", includePath);

    // Issue: this adds -system includes which can break system libraries includes.
    //fprintf(file, "set(CMAKE_C_STANDARD_INCLUDE_DIRECTORIES \"%s/include\")\n", includePath);
    //fprintf(file, "set(CMAKE_CXX_STANDARD_INCLUDE_DIRECTORIES \"%s/include\")\n", includePath);
}

static int __generate_cmake_file(const char* path, struct oven_backend_data* data)
{
    FILE* file;
    int   status;

    file = fopen(path, "w");
    if(!file) {
        VLOG_ERROR("cmake", "failed to open workspace.cmake for writing: %s\n", strerror(errno));
        return -1;
    }

    status = __write_header(file, data->project_name, data->profile_name);
    if (status != 0) {
        goto cleanup;
    }

    /*if (data->paths.ingredients) {
        if (strcmp(data->platform.target_platform, "linux") == 0) {
            __write_linux_prefix(file, data->paths.ingredients);
            __write_linux_include(file, data->paths.ingredients);
        } else if (strcmp(data->platform.target_platform, "windows") == 0) {
            __write_windows_prefix(file, data->paths.ingredients);
            __write_windows_include(file, data->paths.ingredients);
        } else {
            __write_default_prefix(file, data->paths.ingredients);
            __write_default_include(file, data->paths.ingredients);
        }
    }*/

cleanup:
    fclose(file);
    return status;
}

// TODO this exists in both autotools and cmake backend, move to a common
// directory?
static const char* __get_cmake_default_install_path(const char* platform)
{
    if (strcmp(platform, "windows") == 0) {
        return "Program Files";
    } else if (strcmp(platform, "linux") == 0) {
        return "/usr";
    } else if (strcmp(platform, "vali") == 0) {
        return "";
    } else {
        return "";
    }
}

static char* __replace_install_prefix(const char* previousValue, const char* platform, struct oven_backend_data_paths* paths)
{
    // On new values, just return the default for the platform
    if (previousValue == NULL) {
        return strpathcombine(paths->install, __get_cmake_default_install_path(platform));
    }
    
    // On existing, there are two cases
    // 1. They already specified the INSTALL prefix
    // 2. They have a normal path specified.
    if (strncmp(previousValue, paths->install, strlen(paths->install))) {
        // no, let us modify
        return strpathcombine(paths->install, previousValue);
    }
    // it seems this was set up correctly by the recipe, let it be
    return platform_strdup(previousValue);
}

static void __add_default_prefix_paths(char* output, const char* platform, const char* buildIngredientsRoot)
{
    if (strcmp(platform, "windows") == 0) {
        // TODO
    } else if (strcmp(platform, "linux") == 0) {
        // Add defaults like the root and /usr prefix
        strcat(output, buildIngredientsRoot);
        strcat(output, g_pathSeperator);
        strcat(output, buildIngredientsRoot);
        strcat(output, "/usr");
    } else if (strcmp(platform, "vali") == 0) {
        strcat(output, buildIngredientsRoot);
    }
}

static char* __replace_path_prefix(const char* previousValue, const char* platform, struct oven_backend_data_paths* paths)
{
    char   tmp[4096] = { 0 };
    size_t length;

    // Expect generally that people don't modify this
    // However if they do, let us produce a modified version
    if (previousValue == NULL) {
        __add_default_prefix_paths(&tmp[0], platform, paths->build_ingredients);
        return platform_strdup(&tmp[0]);
    }

    strcat(tmp, previousValue);
    length = strlen(tmp);
    tmp[length++] = g_pathSeperator[0];
    __add_default_prefix_paths(&tmp[length], platform, paths->build_ingredients);
    return platform_strdup(&tmp[0]);
}

static char* __extract_cmake_option_value(const char* startOfOption)
{
    char* startOfValue;
    char* endOfValue;
    char* oldValue;
    char* newValue;

    startOfValue = strchr(startOfOption, '=') + 1;
    endOfValue   = strchr(startOfValue, ' ');
    if (endOfValue == NULL) {
        endOfValue = strchr(startOfValue, '\0');
        if (endOfValue == NULL) {
            return NULL;
        }
    }
    return platform_strndup(startOfValue, endOfValue - startOfValue);
}

static void __replace_cmake_option_value(char* arguments, const char* option, const char* value)
{
    size_t length = strlen(arguments);
    size_t optionStartIndex;
    size_t optionValueStartIndex;
    size_t optionValueEndIndex;
    char*  endOfOption;

    // find start of the option
    optionStartIndex = (size_t)(strstr(arguments, option) - arguments);
    optionValueStartIndex = (size_t)((strchr(&arguments[optionStartIndex], '=') + 1) - arguments);

    // at this point, find end of option+value
    endOfOption = strchr(&arguments[optionValueStartIndex], ' ');
    if (endOfOption == NULL) {
        endOfOption = strchr(&arguments[optionValueStartIndex], '\0');
        if (endOfOption == NULL) {
            VLOG_ERROR("cmake",  "failed to find end of option: %s\n", option);
            return;
        }
    }
    optionValueEndIndex = (size_t)(endOfOption - arguments);

    // Now we have the indices, then we can calculate the current length, and the new length
    size_t currentLength = optionValueEndIndex - optionValueStartIndex;
    size_t newLength = strlen(value);
    if (currentLength > newLength) {
        // we need to replace, then shorten the argument
        memcpy(&arguments[optionValueStartIndex], value, newLength);
        memcpy(&arguments[optionValueStartIndex + newLength], endOfOption, length - optionValueEndIndex);

        // since the string is now shorter, make sure it gets terminated
        arguments[length - (currentLength - newLength)] = '\0';
    } else if (currentLength == newLength) {
        memcpy(&arguments[optionValueStartIndex], value, newLength);
    } else {
        // new length is longer, we need to move it, then replace
        char* remaining = platform_strdup(&arguments[optionValueEndIndex]);
        if (remaining == NULL) {
            VLOG_ERROR("cmake", "failed to replace option %s with %s\n", option, value);
            return;
        }
        memcpy(&arguments[optionValueStartIndex], value, newLength);
        memcpy(&arguments[optionValueStartIndex + newLength], remaining, length - optionValueEndIndex);
        free(remaining);
        
        // since the string is now longer, make sure it gets terminated
        arguments[length + (newLength - currentLength)] = '\0';
    }
}

static char* __replace_or_add_cmake_prefixes(const char* platform, const char* arguments, struct oven_backend_data_paths* paths)
{
    char* newArguments;
    struct {
        const char* prefix;
        char* (*replace)(const char* previousValue, const char* platform, struct oven_backend_data_paths* paths);
    } prefixes[] = {
        { "CMAKE_INSTALL_PREFIX", __replace_install_prefix },
        { "CMAKE_PREFIX_PATH", __replace_path_prefix },
        { NULL, NULL }
    };

    // allocate new space for arguments
    newArguments = malloc(strlen(arguments) + 4096);
    if (newArguments == NULL) {
        return NULL;
    }
    memset(newArguments, 0, strlen(arguments) + 4096);
    strcpy(newArguments, arguments);

    for (int i = 0; prefixes[i].prefix != NULL; i++) {
        char* exists = strstr(newArguments, prefixes[i].prefix);
        char* oldValue;
        char* newValue;
        int   status;

        // The value did not exist, let us add it
        if (!exists) {
            newValue = prefixes[i].replace(NULL, platform, paths);
            if (newValue == NULL) {
                VLOG_ERROR("cmake", "failed to add option %s\n", prefixes[i].prefix);
                free(newArguments);
                return NULL;
            }
            strcat(newArguments, " -D");
            strcat(newArguments, prefixes[i].prefix);
            strcat(newArguments, "=");
            strcat(newArguments, newValue);
            free(newValue);
            continue;
        }

        oldValue = __extract_cmake_option_value(exists);
        if (oldValue == NULL) {
            VLOG_ERROR("cmake", "failed to get existing option value of %s\n", prefixes[i].prefix);
            free(newArguments);
            return NULL;
        }

        newValue = prefixes[i].replace(oldValue, platform, paths);
        if (newValue == NULL) {
            VLOG_ERROR("cmake", "failed to replace option %s\n", prefixes[i].prefix);
            free(oldValue);
            free(newArguments);
            return NULL;
        }
        __replace_cmake_option_value(newArguments, prefixes[i].prefix, newValue);
        free(oldValue);
        free(newValue);
    }
    return newArguments;
}

static void __meson_output_handler(const char* line, enum platform_spawn_output_type type) 
{
    if (type == PLATFORM_SPAWN_OUTPUT_TYPE_STDOUT) {
        VLOG_DEBUG("cmake", line);
    } else {
        VLOG_ERROR("cmake", line);
    }
}

#if 0
static void __use_workspace_file(struct oven_backend_data* data)
{
    char*  argument = NULL;
    size_t argumentLength;
    char*  workspacePath;
    int    status = -1;
    int    written;

    workspacePath = strpathcombine(data->paths.build, "workspace.cmake");
    if (workspacePath == NULL) {
        return;
    }


    status = __generate_cmake_file(workspacePath, data);
    if (status != 0) {
        return;
    }

    // build the cmake command, execute from build folder
    // if cross compiling set -DCMAKE_FIND_USE_CMAKE_SYSTEM_PATH=OFF
    written = snprintf(
        argument,
        argumentLength - 1,
        "%s -DCMAKE_PROJECT_INCLUDE=%s %s",
        NULL,
        workspacePath,
        data->paths.project
    );
    argument[written] = '\0';
}
#endif

int cmake_main(struct oven_backend_data* data, union chef_backend_options* options)
{
    char*  argument    = NULL;
    char*  newArguments;
    char** environment = NULL;
    int    written;
    int    status = -1;
    size_t argumentLength;

    newArguments = __replace_or_add_cmake_prefixes(
        data->platform.target_platform,
        data->arguments,
        &data->paths
    );
    if (newArguments == NULL) {
        free(newArguments);
        return -1;
    }

    argumentLength = strlen(newArguments) + PATH_MAX;
    argument       = malloc(argumentLength);
    if (argument == NULL) {
        goto cleanup;
    }

    environment = environment_create(data->process_environment, data->environment);
    if (environment == NULL) {
        free(argument);
        goto cleanup;
    }

    written = snprintf(
        argument,
        argumentLength - 1,
        "-S %s -B %s %s",
        data->paths.project,
        data->paths.build,
        newArguments
    );
    argument[written] = '\0';

    // perform the spawn operation
    VLOG_DEBUG("cmake", "executing 'cmake %s'\n", argument);
    vlog_set_output_options(stdout, VLOG_OUTPUT_OPTION_RETRACE);
    status = platform_spawn(
        "cmake",
        argument,
        (const char* const*)environment,
        &(struct platform_spawn_options) {
            .cwd = data->paths.build,
            .output_handler = __meson_output_handler
        }
    );
    vlog_clear_output_options(stdout, VLOG_OUTPUT_OPTION_RETRACE);
    
cleanup:
    environment_destroy(environment);
    free(argument);
    free(newArguments);
    return status;
}
