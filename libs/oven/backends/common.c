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
#include <chef/environment.h>
#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <vlog.h>

int backend_args_add(struct backend_args* args, const char* value)
{
    char** grown;
    char*  copy;

    if (args == NULL || value == NULL) {
        errno = EINVAL;
        return -1;
    }

    // Reserve space for the new value and the terminating NULL pointer.
    if (args->count > SIZE_MAX / sizeof(char*) - 2) {
        errno = EOVERFLOW;
        return -1;
    }

    copy = platform_strdup(value);
    if (copy == NULL) {
        return -1;
    }

    grown = realloc(args->values, (args->count + 2) * sizeof(char*));
    if (grown == NULL) {
        free(copy);
        return -1;
    }

    args->values = grown;
    args->values[args->count++] = copy;
    args->values[args->count] = NULL;
    return 0;
}

int backend_args_pair(struct backend_args* args, const char* key, const char* value)
{
    size_t keyLength;
    size_t valueLength;
    char*  joined;
    int    status;

    if (args == NULL || key == NULL || value == NULL) {
        errno = EINVAL;
        return -1;
    }

    keyLength = strlen(key);
    valueLength = strlen(value);
    
    // The joined option needs one additional byte for its NULL terminator.
    if (keyLength > SIZE_MAX - valueLength - 1) {
        errno = EOVERFLOW;
        return -1;
    }

    joined = malloc(keyLength + valueLength + 1);
    if (joined == NULL) {
        return -1;
    }

    memcpy(joined, key, keyLength);
    memcpy(joined + keyLength, value, valueLength + 1);
    status = backend_args_add(args, joined);
    free(joined);
    return status;
}

int backend_args_parse(struct backend_args* args, const char* text)
{
    char*  copy;
    char** parsed;
    int    status = 0;

    if (args == NULL) {
        errno = EINVAL;
        return -1;
    }

    copy = text != NULL ? platform_strdup(text) : NULL;
    if (text != NULL && copy == NULL) {
        return -1;
    }

    parsed = strargv(copy, NULL, NULL);
    if (parsed == NULL) {
        free(copy);
        return -1;
    }

    // Make a copy of each parsed token into the backend
    for (size_t i = 0; parsed[i] != NULL; i++) {
        if (backend_args_add(args, parsed[i]) != 0) {
            status = -1;
            break;
        }
    }

    strargv_free(parsed);
    free(copy);
    return status;
}

void backend_args_destroy(struct backend_args* args)
{
    if (args == NULL) {
        return;
    }

    for (size_t i = 0; i < args->count; i++) {
        free(args->values[i]);
    }
    free(args->values);
    memset(args, 0, sizeof(struct backend_args));
}

int backend_validate(const struct oven_backend_data* data)
{
    if (data == NULL || data->paths.source == NULL || data->paths.build == NULL ||
        data->paths.install == NULL || data->paths.build_ingredients == NULL ||
        data->platform.target_platform == NULL || data->paths.build[0] == '\0' ||
        data->paths.install[0] == '\0') {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

// Proxy logging output of a child process to the oven logger.
static void __output(const char* line, enum platform_spawn_output_type type)
{
    if (type == PLATFORM_SPAWN_OUTPUT_TYPE_STDOUT) {
        VLOG_DEBUG("oven", "%s", line);
    } else {
        VLOG_ERROR("oven", "%s", line);
    }
}

int backend_run(struct oven_backend_data* data, const char* executable,
    struct backend_args* args, const char* cwd, struct list* overrides)
{
    struct list empty = { 0 };
    const char* emptyParent[] = { NULL };
    char**      environment;
    int         status;

    if (data == NULL || executable == NULL || args == NULL || cwd == NULL) {
        errno = EINVAL;
        return -1;
    }

    environment = environment_create(
        data->process_environment != NULL ? data->process_environment : emptyParent,
        data->environment != NULL ? data->environment : &empty
    );
    if (environment == NULL) {
        return -1;
    }

    // Rebuild the environment only when this invocation supplies overrides.
    if (overrides != NULL) {
        char** updated = environment_create((const char* const*)environment, overrides);
        environment_destroy(environment);
        environment = updated;
        if (environment == NULL) {
            return -1;
        }
    }

    VLOG_DEBUG("oven", "executing %s in %s\n", executable, cwd);

    status = platform_spawn_argv(
        executable,
        (const char* const*)args->values,
        (const char* const*)environment, 
        &(struct platform_spawn_options) {
            .cwd = cwd,
            .argv0 = NULL,
            .output_handler = __output,
        }
    );
    if (status) {
        VLOG_ERROR("oven", "%s failed (status %d)\n", executable, status);
    }

    environment_destroy(environment);
    return status;
}

static char* __normalize(const char* path)
{
    char* copy = platform_strdup(path);
    if (copy == NULL) {
        return NULL;
    }

    // Normalize both Windows and POSIX separators before comparing paths.
    for (char* p = copy; *p != '\0'; p++) {
        if (*p == '\\') {
            *p = '/';
        }
    }
    return copy;
}

static int __validate_prefix(const char* prefix)
{
    // Reject parent components before combining the prefix with the root
    for (const char* p = prefix; *p != '\0';) {
        const char* end;

        // Skip repeated separators
        while (*p == '/') {
            p++;
        }

        end = strchr(p, '/');
        if (end == NULL) {
            end = p + strlen(p);
        }

        // Protect against paths trying to escape the root
        if (end - p == 2 && p[0] == '.' && p[1] == '.') {
            errno = EINVAL;
            return -1;
        }

        p = end;
    }
    return 0;
}

char* backend_install_prefix(const char* root, const char* prefix)
{
    char* base;
    char* value;
    char* result = NULL;
    const char* relative;
    int match;
    size_t length;

    if (root == NULL || root[0] == '\0' || prefix == NULL) {
        errno = EINVAL;
        return NULL;
    }

    base = __normalize(root);
    value = __normalize(prefix);
    if (base == NULL || value == NULL) {
        goto cleanup;
    }

    length = strlen(base);

    // Remove trailing separators to canonicalize the root path
    while (length > 1 && base[length - 1] == '/') {
        base[--length] = '\0';
    }

    // Do some basic validation on the prefix path
    if (__validate_prefix(value) != 0) {
        goto cleanup;
    }

#if CHEF_ON_WINDOWS
    match = _strnicmp(value, base, length) == 0;
#else
    match = strncmp(value, base, length) == 0;
#endif

    // Preserve an already-staged prefix only when the root ends at a path boundary
    if (match && (base[length - 1] == '/' || value[length] == '/' || value[length] == '\0')) {
        result = value;
        value = NULL;
        goto cleanup;
    }

    relative = value;
    
    // Strip a Windows drive marker before combining an absolute-looking value.
    if (relative[0] != '\0' && isalpha((unsigned char)relative[0]) && relative[1] == ':') {
        relative += 2;
    }

    // Remove leading separators so its passed as relative to strpathcombine
    while (*relative == '/') {
        relative++;
    }

    result = strpathcombine(base, relative);

cleanup:
    free(base);
    free(value);
    return result;
}

const char* backend_default_prefix(const char* platform, const char* linuxDefault)
{
    if (platform == NULL) {
        return "";
    }

    if (strcmp(platform, "windows") == 0) {
        return "Program Files";
    }

    if (strcmp(platform, "linux") == 0) {
        return linuxDefault;
    }

    return "";
}

static int __add_fallback_prefix(struct backend_args* args, const char* root, const char* fallback)
{
    char* staged;
    int   status;

    staged = backend_install_prefix(root, fallback);
    if (staged == NULL) {
        return -1;
    }

    status = backend_args_pair(args, "--prefix=", staged);
    free(staged);
    return status;
}

int backend_rewrite_prefix(struct backend_args* args, const char* root, const char* fallback)
{
    int found = 0;

    if (args == NULL || root == NULL || fallback == NULL) {
        errno = EINVAL;
        return -1;
    }

    for (size_t i = 0; i < args->count; i++) {
        struct backend_args replacement = { 0 };
        char*               staged;
        int                 status;
        const char*         value;
        int                 separate;

        separate = strcmp(args->values[i], "--prefix") == 0;
        if (separate) {
            // Skip the "--prefix" argument itself
            i++;
            
            // When the --prefix is separate, the next argument must be its value.
            if (i == args->count) {
                errno = EINVAL;
                return -1;
            }

            value = args->values[i];
        } else if (strncmp(args->values[i], "--prefix=", 9) == 0) {
            value = args->values[i] + 9;
        } else {
            // Ignore unrelated Autotools options
            continue;
        }

        staged = backend_install_prefix(root, value);
        if (staged == NULL) {
            return -1;
        }

        status = backend_args_pair(&replacement, separate ? "" : "--prefix=", staged);
        free(staged);
        if (status) {
            backend_args_destroy(&replacement);
            return -1;
        }

        free(args->values[i]);
        args->values[i] = replacement.values[0];
        free(replacement.values);
        found = 1;
    }

    if (found) {
        return 0;
    }

    // Add a default prefix only when the recipe did not provide one.
    return __add_fallback_prefix(args, root, fallback);
}
