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

#ifndef __CHEF_PACKAGE_MANIFEST_H__
#define __CHEF_PACKAGE_MANIFEST_H__

#include <chef/package.h>
#include <stddef.h>

struct VaFs;

/**
 * @brief Chef package metadata model and codec.
 *
 * This header extends `chef/package.h` with the manifest representation used
 * for reading and writing Chef metadata inside `.pack` images.
 */

struct chef_package_blob {
    const void* data;
    size_t      size;
};

struct chef_package_string_array {
    const char** values;
    size_t       count;
};

struct chef_package_manifest_command {
    enum chef_command_type  type;
    const char*             name;
    const char*             description;
    const char*             arguments;
    const char*             path;
    struct chef_package_blob icon;
};

enum chef_package_manifest_capability_type {
    CHEF_PACKAGE_MANIFEST_CAPABILITY_NONE = 0,
    CHEF_PACKAGE_MANIFEST_CAPABILITY_ALLOW_LIST = 1
};

struct chef_package_manifest_capability {
    const char*                                name;
    enum chef_package_manifest_capability_type type;
    struct chef_package_string_array           allow_list;
};

struct chef_package_manifest_application_config {
    const char* network_gateway;
    const char* network_dns;
};

struct chef_package_manifest_ingredient_config {
    struct chef_package_string_array bin_dirs;
    struct chef_package_string_array inc_dirs;
    struct chef_package_string_array lib_dirs;
    struct chef_package_string_array compiler_flags;
    struct chef_package_string_array linker_flags;
};

/**
 * @brief Canonical Chef package metadata model.
 *
 * This struct represents the Chef-specific metadata embedded in a `.pack`
 * (VaFs image). It does not describe the image contents themselves.
 *
 * For write operations the caller owns all referenced memory.
 * For load operations the library allocates the manifest and all nested data;
 * release it with chef_package_manifest_free().
 */
struct chef_package_manifest {
    const char* name;
    const char* platform;
    const char* architecture;
    enum chef_package_type type;
    const char* base;
    const char* summary;
    const char* description;
    const char* license;
    const char* eula;
    const char* maintainer;
    const char* maintainer_email;
    const char* homepage;

    struct chef_version      version;
    struct chef_package_blob icon;

    struct chef_package_manifest_command*    commands;
    size_t                                   commands_count;
    struct chef_package_manifest_application_config application;
    struct chef_package_manifest_ingredient_config  ingredient;
    struct chef_package_manifest_capability* capabilities;
    size_t                                   capabilities_count;
};

/**
 * @brief Parses a package version string into a structured version.
 *
 * Supports the package format used by the writer today:
 * - `major.minor.patch`
 * - `major.minor.patch+tag`
 * - `revision`
 *
 * When a tag is present it is preserved verbatim, including the leading `+`,
 * to match the existing on-disk encoding.
 *
 * @param[In]  string     Version string to parse.
 * @param[Out] versionOut Parsed version fields.
 * @return 0 on success, -1 on failure with errno set.
 */
extern int chef_package_manifest_parse_version(
    const char*          string,
    struct chef_version* versionOut);

/**
 * @brief Loads Chef package metadata from an open VaFs image.
 *
 * This function reads Chef-specific metadata features only. It does not close
 * the supplied VaFs handle.
 *
 * @param[In]  vafs        Open VaFs handle.
 * @param[Out] manifestOut Receives a newly allocated manifest.
 * @return 0 on success, otherwise a non-zero error code.
 */
extern int chef_package_manifest_load_vafs(
    struct VaFs*                  vafs,
    struct chef_package_manifest** manifestOut);

/**
 * @brief Loads Chef package metadata from a `.pack` file.
 *
 * @param[In]  path        Path to the package file.
 * @param[Out] manifestOut Receives a newly allocated manifest.
 * @return 0 on success, otherwise a non-zero error code.
 */
extern int chef_package_manifest_load(
    const char*                   path,
    struct chef_package_manifest** manifestOut);

/**
 * @brief Writes Chef package metadata into an open VaFs image.
 *
 * The image must already have been created. This function only serializes the
 * Chef metadata features; it does not add files or directories to the image.
 *
 * @param[In] vafs     Open VaFs handle to write features into.
 * @param[In] manifest Metadata to serialize.
 * @return 0 on success, -1 on failure.
 */
extern int chef_package_manifest_write(
    struct VaFs*                        vafs,
    const struct chef_package_manifest* manifest);

/**
 * @brief Releases a manifest returned by the load functions.
 *
 * Passing NULL is allowed and has no effect.
 */
extern void chef_package_manifest_free(struct chef_package_manifest* manifest);

#endif //!__CHEF_PACKAGE_MANIFEST_H__