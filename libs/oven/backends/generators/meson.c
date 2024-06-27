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
#include <chef/platform.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <utils.h>
#include <vlog.h>

// import this while we find a suitiable place
extern char* oven_preprocess_text(const char* original);

static char* __processed_path(struct oven_backend_data* data)
{
    return strpathcombine(data->paths.build, "cross-file.txt");
}

static int __read_file(const char* path, char** bufferOut)
{
    FILE* file;
    long  size;
    char* buffer;

    file = fopen(path, "rb");
    if (file == NULL) {
        fprintf(stderr, "Failed to open %s for reading: %s\n", path, strerror(errno));
        return -1;
    }

    fseek(file, 0, SEEK_END);
    size = ftell(file);
    rewind(file);

    buffer = malloc(size);
    if (buffer == NULL) {
        fprintf(stderr, "Failed to read %s: %s\n", path, strerror(errno));
        fclose(file);
        return -1;
    }

    fread(buffer, 1, size, file);
    fclose(file);
    return 0;
}

static int __write_file(const char* path, const char* buffer)
{
    FILE* file;

    file = fopen(path, "w");
    if (file == NULL) {
        fprintf(stderr, "Failed to open %s for writing: %s\n", path, strerror(errno));
        return -1;
    }

    fputs(buffer, file);
    fclose(file);
    return 0;
}

static char* __compute_arguments(struct oven_backend_data* data, union oven_backend_options* options)
{
    size_t length;
    char*  args;
    int    status;
    FILE*  stream;

    stream = open_memstream(&args, &length);
    if (stream == NULL) {
        return NULL;
    }

    if (options->meson.cross_file != NULL) {
        char* original, *processed, *path;
        
        path = __processed_path(data);
        if (path == NULL) {
            return NULL;
        }

        /**
         * @brief The cross-file we take in, is a template. We will be pre-processing it a bit
         * before writing a final cross-file to handle any variables present.
         */
        status = __read_file(options->meson.cross_file, &original);
        if (status) {
            free(path);
            return NULL;
        }

        processed = oven_preprocess_text(original);
        if (processed == NULL) {
            free(path);
            free(original);
            return NULL;
        }

        status = __write_file(path, processed);
        free(original);
        free(processed);

        if (status) {
            free(path);
            return NULL;
        }
        fprintf(stream, "--cross-file %s", path);
        free(path);
    }

    // --force-fallback-for=llvm

    fclose(stream);
    return args;
}

static void __meson_output_handler(const char* line, enum platform_spawn_output_type type) 
{
    if (type == PLATFORM_SPAWN_OUTPUT_TYPE_STDOUT) {
        VLOG_TRACE("meson", line);

        // re-enable again if it continues to print
        vlog_set_output_options(stdout, VLOG_OUTPUT_OPTION_RETRACE);
    } else {
        // clear retrace on error output
        vlog_clear_output_options(stdout, VLOG_OUTPUT_OPTION_RETRACE);
        
        VLOG_ERROR("meson", line);
    }
}

int meson_config_main(struct oven_backend_data* data, union oven_backend_options* options)
{
    char*  finalArguments = NULL;
    char** environment  = NULL;
    char*  args         = NULL;
    int    status = -1;
    size_t length;

    environment = oven_environment_create(data->process_environment, data->environment);
    if (environment == NULL) {
        errno = ENOMEM;
        return -1;
    }

    // lets make it 128 to cover some extra grounds
    length = 128 + strlen(data->arguments) + strlen(data->paths.project);
    args = __compute_arguments(data, options);
    if (args) {
        length += strlen(args);
    }
    
    finalArguments = malloc(length);
    if (finalArguments == NULL) {
        errno = ENOMEM;
        goto cleanup;
    }

    if (args) {
        snprintf(finalArguments, length, "setup %s %s %s", data->arguments, args, data->paths.project);
    } else {
        snprintf(finalArguments, length, "setup %s %s", data->arguments, data->paths.project);
    }

    VLOG_TRACE("meson", "executing 'meson %s'\n", finalArguments);
    vlog_set_output_options(stdout, VLOG_OUTPUT_OPTION_RETRACE);
    status = platform_spawn(
        "meson",
        finalArguments,
        (const char* const*)environment,
        &(struct platform_spawn_options) {
            .cwd = data->paths.build,
            .output_handler = __meson_output_handler
        }
    );
    vlog_clear_output_options(stdout, VLOG_OUTPUT_OPTION_RETRACE);

cleanup:
    free(args);
    free(finalArguments);
    oven_environment_destroy(environment);
    return status;
}
