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
#include <errno.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief Build the semicolon-separated CMake prefix search path.
 *
 * Existing user-provided entries are kept first. The ingredient root and the
 * platform-specific subdirectories are then appended so that CMake can find
 * dependencies staged by Chef.
 */
static char* __prefix_paths(const char* previous, struct oven_backend_data* data)
{
    struct backend_args paths = { 0 };
    const char* suffixes[] = { "", NULL, NULL, NULL };
    char* result = NULL;
    size_t length;

    // Add the directories where Linux dependency packages are staged.
    if (strcmp(data->platform.target_platform, "linux") == 0) {
        suffixes[1] = "usr";
        suffixes[2] = "usr/local";
    } else if (strcmp(data->platform.target_platform, "windows") == 0) {
        // Windows packages conventionally use the Program Files suffix.
        suffixes[1] = "Program Files";
    }

    // Keep a user-supplied search path before Chef's staged dependency paths.
    if (previous != NULL && previous[0] != '\0' &&
        backend_args_add(&paths, previous) != 0) {
        goto cleanup;
    }

    // Convert each staged directory into one CMake list entry.
    for (size_t i = 0; suffixes[i] != NULL; i++) {
        char* path;
        int status;

        path = strpathcombine(data->paths.build_ingredients, suffixes[i]);
        if (path == NULL) {
            goto cleanup;
        }

        status = backend_args_add(&paths, path);
        free(path);
        // Stop before returning an incomplete CMake search path.
        if (status != 0) {
            goto cleanup;
        }
    }

    // CMake variables are lists separated by semicolons on every host OS.
    result = strflatten((const char* const*)paths.values, ";", &length);

cleanup:
    backend_args_destroy(&paths);
    return result;
}

/**
 * @brief Rewrite the supported CMake path definitions for the staging root.
 *
 * Definitions are matched by their complete variable name. This deliberately
 * allows CMake type suffixes, such as :PATH, while leaving lookalike variables
 * untouched. A definition without a value is rejected because it cannot be
 * safely staged.
 */
static int __rewrite_options(struct backend_args* args, struct oven_backend_data* data)
{
    const char* names[] = { "CMAKE_INSTALL_PREFIX", "CMAKE_PREFIX_PATH" };
    int found[2] = { 0 };

    // Inspect each recipe option because either supported definition may be present.
    for (size_t i = 0; i < args->count; i++) {
        const char* definition;
        size_t offset = 2;

        // A standalone -D consumes the following token as its definition.
        if (strcmp(args->values[i], "-D") == 0) {
            i++;
            // A standalone -D must have a following definition token.
            if (i == args->count) {
                errno = EINVAL;
                return -1;
            }
            offset = 0;
        } else if (strncmp(args->values[i], "-D", 2) != 0) {
            // Only CMake definitions can change where files are staged.
            continue;
        }

        definition = args->values[i] + offset;
        // Compare the definition against both staging-sensitive CMake variables.
        for (size_t key = 0; key < 2; key++) {
            size_t length = strlen(names[key]);
            const char* equal;
            char* value;
            char* option;
            struct backend_args replacement = { 0 };
            int status;

            if (strncmp(definition, names[key], length) != 0) {
                continue;
            }

            // Reject lookalike variable names such as CMAKE_PREFIX_PATH_EXTRA.
            if (definition[length] != '\0' && definition[length] != '=' &&
                definition[length] != ':') {
                continue;
            }

            equal = strchr(definition + length, '=');
            // A staging-sensitive definition must provide a value to rewrite.
            if (equal == NULL) {
                errno = EINVAL;
                return -1;
            }

            value = key == 0
                ? backend_install_prefix(data->paths.install, equal + 1)
                : __prefix_paths(equal + 1, data);
            if (value == NULL) {
                return -1;
            }

            option = platform_strndup(args->values[i], equal - args->values[i] + 1);
            if (option == NULL) {
                free(value);
                return -1;
            }

            status = backend_args_pair(&replacement, option, value);
            free(option);
            free(value);
            // Replace the original token only after the new token is complete.
            if (status != 0) {
                backend_args_destroy(&replacement);
                return -1;
            }

            free(args->values[i]);
            args->values[i] = replacement.values[0];
            free(replacement.values);
            found[key] = 1;
            break;
        }
    }

    // Add defaults for staging-sensitive variables omitted by the recipe.
    for (size_t key = 0; key < 2; key++) {
        char* value;
        int status;

        // Preserve an explicitly supplied definition from the previous pass.
        if (found[key] != 0) {
            continue;
        }

        value = key == 0
            ? backend_install_prefix(data->paths.install,
                backend_default_prefix(data->platform.target_platform, "/usr"))
            : __prefix_paths(NULL, data);
        if (value == NULL) {
            return -1;
        }

        status = backend_args_pair(args,
            key == 0 ? "-DCMAKE_INSTALL_PREFIX=" : "-DCMAKE_PREFIX_PATH=", value);
        free(value);
        // Fail rather than invoking CMake with a partially rewritten command.
        if (status != 0) {
            return -1;
        }
    }

    return 0;
}

int cmake_main(struct oven_backend_data* data, union chef_backend_options* options)
{
    struct backend_args args = { 0 };
    int                 status;
    (void)options;

    status = backend_validate(data);
    if (status) {
        return status;
    }

    // Build the configure command in the order CMake expects its arguments.
    if (backend_args_add(&args, "-S") != 0 ||
        backend_args_add(&args, data->paths.source) != 0 ||
        backend_args_parse(&args, data->arguments) != 0 ||
        __rewrite_options(&args, data) != 0) {
        status = -1;
        goto cleanup;
    }

    status = backend_run(data, "cmake", &args, data->paths.build, NULL);

cleanup:
    backend_args_destroy(&args);
    return status;
}

/**
 * @brief Run one CMake operation against the configured build tree.
 */
static int __command(struct oven_backend_data* data, const char* operation, const char* arguments)
{
    struct backend_args args = { 0 };
    int                 status = -1;

    if (backend_args_add(&args, operation) != 0 ||
        backend_args_add(&args, ".") != 0 ||
        backend_args_add(&args, "--config") != 0 ||
        backend_args_add(&args, data->profile_name != NULL ? data->profile_name : "Release") != 0 ||
        backend_args_parse(&args, arguments) != 0) {
        goto cleanup;
    }

    status = backend_run(data, "cmake", &args, data->paths.build, NULL);

cleanup:
    backend_args_destroy(&args);
    return status;
}

int cmake_build_main(struct oven_backend_data* data, union chef_backend_options* options)
{
    int status;
    (void)options;

    status = backend_validate(data);
    if (status) {
        return status;
    }

    // Install only after the build command has completed successfully.
    status = __command(data, "--build", data->arguments);
    if (status) {
        return status;
    }
    return __command(data, "--install", NULL);
}

int cmake_clean_main(struct oven_backend_data* data, union chef_backend_options* options)
{
    int status;
    (void)options;

    status = backend_validate(data);
    if (status) {
        return status;
    }
    return __command(data, "--build", "--target clean");
}
