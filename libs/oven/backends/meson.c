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
#include <liboven.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <vlog.h>

extern char* oven_preprocess_text(const char* original);

static int __write_file(const char* path, const char* buffer)
{
    FILE* file;
    int status;

    VLOG_DEBUG("meson", "__write_file(path=%s)\n", path ? path : "(null)");

    file = fopen(path, "w");
    if (file == NULL) {
        fprintf(stderr, "Failed to open %s for writing: %s\n", path, strerror(errno));
        return -1;
    }

    status = fputs(buffer, file) == EOF ? -1 : 0;
    if (fclose(file)) {
        status = -1;
    }
    return status;
}

/**
 * @brief Create a Meson cross-file template.
 *
 * Relative templates are resolved against the recipe project root rather than
 * the backend working directory. The generated file is written into the build
 * directory and added to the setup command as a separate argument.
 */
static int __cross_file(
    struct oven_backend_data*   data,
    union chef_backend_options* options,
    struct backend_args*        args)
{
    char*       input = NULL;
    char*       original = NULL;
    size_t      originalLength;
    char*       processed = NULL;
    char*       path = NULL;
    const char* cross;
    int         status = -1;

    if (options == NULL || options->meson.cross_file == NULL) {
        return 0;
    }

    cross = options->meson.cross_file;

    // Relative cross-file templates live alongside the recipe, independent of cwd.
    // Resolve relative templates from the recipe root, not the backend cwd.
    if (cross[0] == '/' || cross[0] == '\\' ||
        (cross[0] != '\0' && cross[1] == ':')) {
        input = platform_strdup(cross);
    } else {
        // A relative template has no meaningful base without the recipe root.
        if (data->paths.root == NULL) {
            errno = EINVAL;
            return -1;
        }

        input = strpathcombine(data->paths.root, cross);
    }

    path = strpathcombine(data->paths.build, "cross-file.txt");
    if (input == NULL || path == NULL) {
        goto cleanup;
    }

    status = platform_readtext(input, &original, &originalLength);
    if (status) {
        goto cleanup;
    }

    processed = oven_preprocess_text(original);
    if (processed == NULL) {
        status = -1;
        goto cleanup;
    }

    status = __write_file(path, processed);
    if (status) {
        goto cleanup;
    }

    if (backend_args_add(args, "--cross-file") != 0 || backend_args_add(args, path) != 0) {
        status = -1;
        goto cleanup;
    }

    status = 0;

cleanup:
    free(input);
    free(original);
    free(processed);
    free(path);
    return status;
}

// Convert Meson's -Dprefix spellings to --prefix.
static int __prefix_options(struct backend_args* args)
{
    // Meson also accepts the built-in prefix option through -Dprefix=VALUE.
    // Normalize each supported prefix spelling before installing the defaults.
    for (size_t i = 0; i < args->count; i++) {
        struct backend_args replacement = { 0 };
        const char*         value = NULL;
        int                 separate = 0;

        // Skip the "-Dprefix=" part
        // Or skip the -D prefix= part
        if (strncmp(args->values[i], "-Dprefix=", 9) == 0) {
            value = args->values[i] + 9;
        } else if (strcmp(args->values[i], "-D") == 0 && i + 1 < args->count &&
            strncmp(args->values[i + 1], "prefix=", 7) == 0) {
            value = args->values[i + 1] + 7;
            separate = 1;
        }
        
        if (value == NULL) {
            continue;
        }

        // Add the value again with the standard --prefix=
        if (backend_args_pair(&replacement, "--prefix=", value) != 0) {
            return -1;
        }

        // replace it in the list
        free(args->values[i]);
        args->values[i] = replacement.values[0];
        free(replacement.values);

        // Remove the consumed prefix= token when -D was provided separately.
        if (separate) {
            free(args->values[i + 1]);
            memmove(&args->values[i + 1], &args->values[i + 2],
                (args->count - i - 1) * sizeof(char*));
            args->count--;
        }
    }
    return 0;
}

int meson_config_main(struct oven_backend_data* data, union chef_backend_options* options)
{
    struct backend_args args = { 0 };
    struct stat         info;
    char*               core = NULL;
    int                 status;

    status = backend_validate(data);
    if (status) {
        return status;
    }

    // Keep all setup failures visible until the backend process runs successfully.
    status = -1;
    core = strpathcombine(data->paths.build, "meson-private/coredata.dat");
    if (core == NULL || backend_args_add(&args, "setup") != 0) {
        goto cleanup;
    }

    // If meson already created it's coredata.dat file, then we reconfigure
    if (stat(core, &info) == 0) {
        // Make sure we update the existing tree instead of creating a new one
        if (backend_args_add(&args, "--reconfigure") != 0) {
            goto cleanup;
        }
    } else if (errno != ENOENT) {
        // Any stat error other than a missing build tree is unexpected.
        goto cleanup;
    }

    // Append directories and user options only after setup mode is selected.
    if (backend_args_add(&args, data->paths.build) != 0 ||
        backend_args_add(&args, data->paths.source) != 0 ||
        backend_args_parse(&args, data->arguments) != 0 || __prefix_options(&args) != 0 ||
        backend_rewrite_prefix(&args, data->paths.install,
            backend_default_prefix(data->platform.target_platform, "/usr/local")) != 0 ||
        __cross_file(data, options, &args) != 0) {
        goto cleanup;
    }

    status = backend_run(data, "meson", &args, data->paths.build, NULL);

cleanup:
    free(core);
    backend_args_destroy(&args);
    return status;
}

/**
 * @brief Run a Meson operation against the configured build directory.
 */
static int __command(struct oven_backend_data* data, const char* command, const char* arguments)
{
    struct backend_args args = { 0 };
    int                 status;

    // Keep Meson's command, build directory, and recipe arguments together.
    if (backend_args_add(&args, command) != 0 || backend_args_add(&args, "-C") != 0 ||
        backend_args_add(&args, data->paths.build) != 0 ||
        backend_args_parse(&args, arguments) != 0) {
        status = -1;
        goto cleanup;
    }

    status = backend_run(data, "meson", &args, data->paths.build, NULL);

cleanup:
    backend_args_destroy(&args);
    return status;
}

int meson_build_main(struct oven_backend_data* data, union chef_backend_options* options)
{
    int status;
    (void)options;

    status = backend_validate(data);
    if (status != 0) {
        return status;
    }

    // Install only after compilation has produced the configured build tree.
    status = __command(data, "compile", data->arguments);
    if (status) {
        return status;
    }

    return __command(data, "install", "--no-rebuild");
}

int meson_clean_main(struct oven_backend_data* data, union chef_backend_options* options)
{
    int status;
    (void)options;
    
    status = backend_validate(data);
    if (status != 0) {
        return status;
    }
    return __command(data, "compile", "--clean");
}
