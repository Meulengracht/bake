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

#ifndef __CHEF_TOOLCHAIN_H__
#define __CHEF_TOOLCHAIN_H__

#include <chef/package_manifest.h>
#include <chef/recipe.h>

struct chef_toolchain {
    char*                         package_name;
    char*                         package_path;
    char*                         unpack_path;
    struct chef_package_manifest* manifest;
    const struct chef_package_manifest_toolchain_target* target;
};

/**
 * @brief Resolve a recipe toolchain reference for the build host.
 *
 * The selected platform is used to expand the special "platform" reference and
 * to select matching target metadata. The host platform and architecture are
 * used only to retrieve the toolchain package.
 */
extern int chef_toolchain_resolve(
    struct recipe*         recipe,
    const char*            reference,
    const char*            toolchainPlatform,
    const char*            hostPlatform,
    const char*            hostArchitecture,
    const char*            toolchainsRoot,
    struct chef_toolchain* toolchainOut);

/**
 * @brief Release the owned values returned by chef_toolchain_resolve().
 */
extern void chef_toolchain_destroy(struct chef_toolchain* toolchain);

#endif //!__CHEF_TOOLCHAIN_H__
