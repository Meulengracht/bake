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

#ifndef CHEF_BACKEND_PRIVATE_H
#define CHEF_BACKEND_PRIVATE_H

#include <backend.h>
#include <chef/platform.h>

/**
 * @brief Owned argument vector used to invoke a backend.
 *
 * Values are copied when added and the vector is always NULL-terminated. The
 * caller must initialize the structure to zero and call backend_args_destroy()
 * on every exit path.
 */
struct backend_args {
    char** values;
    size_t count;
};

/**
 * @brief Append one copied argument to a backend argument vector.
 * @return Zero on success, or -1 with errno set on failure.
 */
extern int backend_args_add(struct backend_args* args, const char* value);

/**
 * @brief Join a key and value, then append the resulting argument.
 * @return Zero on success, or -1 with errno set on failure.
 */
extern int backend_args_pair(struct backend_args* args, const char* key, const char* value);

/**
 * @brief Parse a recipe argument string into individual arguments.
 * @return Zero on success, or -1 with errno set on failure.
 */
extern int backend_args_parse(struct backend_args* args, const char* text);

/**
 * @brief Release all values owned by an argument vector.
 */
extern void backend_args_destroy(struct backend_args* args);

/**
 * @brief Validate the paths required by a backend entry point.
 * @return Zero when the required inputs are valid, or -1/EINVAL otherwise.
 */
extern int backend_validate(const struct oven_backend_data* data);

/**
 * @brief Run a backend executable with its arguments and environment.
 * @return The child status, or -1 when the process could not be started.
 */
extern int backend_run(
    struct oven_backend_data* data,
    const char*               executable,
    struct backend_args*      args,
    const char*               cwd,
    struct list*              overrides);

/**
 * @brief Place an install prefix below a staging root.
 *
 * A prefix already below the root is preserved. Other prefixes are treated as
 * relative lexical paths after drive and leading-separator removal; parent
 * components are rejected before the paths are combined.
 */
extern char* backend_install_prefix(const char* root, const char* prefix);

/**
 * @brief Return the platform-specific default install-prefix suffix.
 */
extern const char* backend_default_prefix(const char* platform, const char* linuxDefault);

/**
 * @brief Rewrite Autotools --prefix options to use the staging root.
 *
 * Both --prefix VALUE and --prefix=VALUE forms are supported. A default is
 * appended when the recipe did not provide either form.
 */
extern int backend_rewrite_prefix(struct backend_args* args, const char* root, const char* fallback);

#endif
