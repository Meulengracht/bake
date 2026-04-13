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

#ifndef __CHEF_PACKAGE_H__
#define __CHEF_PACKAGE_H__

#include <chef/bits/package.h>

/**
 * @brief Shared Chef package types and ownership helpers.
 *
 * Header layering for the package API is intentionally split by concern:
 * - `chef/package.h` exposes shared package structs, proof structs, and free helpers.
 * - `chef/package_manifest.h` exposes the canonical metadata model and codec.
 * - `chef/package_image.h` exposes `.pack` image construction.
 *
 * Include this header when you only need the shared types or cleanup helpers.
 */

/**
 * @brief Cleans up any resources allocated by the package.
 * 
 * @param[In] package A pointer to the package that will be freed. 
 */
extern void chef_package_free(struct chef_package* package);

/**
 * @brief Cleans up a separately owned version structure.
 * 
 * @param[In] version A pointer to the version that will be freed. 
 */
extern void chef_version_free(struct chef_version* version);

/**
 * @brief Cleans up any resources allocated for command arrays.
 *
 * The commands pointer must reference an array of `struct chef_command`, and
 * the caller must provide the number of elements in that array.
 *
 * @param[In] commands A pointer to an array of commands.
 * @param[In] count    The size of the array passed.
 */
extern void chef_commands_free(struct chef_command* commands, int count);

/**
 * @brief Cleans up package application configuration structures.
 */
extern void chef_package_application_config_free(struct chef_package_application_config* appConfig);

/**
 * @brief Cleans up any resources allocated for capabilities.
 */
extern void chef_package_capabilities_free(struct chef_package_capability* capabilities, int count);

/**
 * @brief Cleans up a package proof and all resources owned by it.
 *
 * Releases the proof strings and the proof object itself. Passing NULL is
 * allowed and has no effect.
 *
 * @param[In] proof A pointer to the proof that will be freed.
 */
extern void chef_package_proof_free(struct chef_package_proof* proof);

/**
 * @brief Parses a string containing a Chef package version.
 *
 * This is a convenience alias for the canonical parser in
 * `chef/package_manifest.h`.
 */
extern int chef_version_from_string(const char* string, struct chef_version* version);

#endif //!__CHEF_PACKAGE_H__
