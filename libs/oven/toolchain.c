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

#include <chef/platform.h>
#include <chef/store.h>
#include <chef/toolchain.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <vlog.h>

void chef_toolchain_destroy(struct chef_toolchain* toolchain)
{
    if (toolchain == NULL) {
        return;
    }

    free(toolchain->package_name);
    free(toolchain->package_path);
    free(toolchain->unpack_path);
    chef_package_manifest_free(toolchain->manifest);
    memset(toolchain, 0, sizeof(struct chef_toolchain));
}

static const char* __get_package_path(
    const char* name,
    const char* channel,
    const char* arch,
    const char* platform)
{
    const char* path;
    int         status;

    status = store_package_path(
        &(struct store_package) {
            .name = name,
            .channel = channel,
            .arch = arch,
            .platform = platform
    }, &path);
    if (status) {
        return NULL;
    }
    return path;
}

int chef_toolchain_resolve(
    struct recipe*         recipe,
    const char*            reference,
    const char*            toolchainPlatform,
    const char*            hostPlatform,
    const char*            hostArchitecture,
    const char*            toolchainsRoot,
    struct chef_toolchain* toolchainOut)
{
    const char* packageReference = reference;
    const char* packagePath;
    char*       name = NULL;
    char*       channel = NULL;
    char*       version = NULL;
    int         revision;
    int         status;

    if (reference == NULL || toolchainPlatform == NULL ||
        hostPlatform == NULL || hostArchitecture == NULL ||
        toolchainsRoot == NULL || toolchainOut == NULL) {
        errno = EINVAL;
        return -1;
    }
    memset(toolchainOut, 0, sizeof(struct chef_toolchain));

    // If reference is set to platform, we must find it in the
    // platform list of the recipe.
    if (strcmp(reference, "platform") == 0) {
        packageReference = recipe_find_platform_toolchain(recipe, toolchainPlatform);
        if (packageReference == NULL) {
            errno = ENOENT;
            return -1;
        }
    }

    // reference is expected to be in the format "publisher/name=channel"
    status = recipe_parse_platform_toolchain(
        packageReference,
        &name,
        &channel,
        &version
    );
    if (status) {
        return status;
    }

    packagePath = __get_package_path(name, channel, hostArchitecture, hostPlatform);
    if (packagePath == NULL) {
        goto cleanup;
    }

    status = chef_package_manifest_load(packagePath, &toolchainOut->manifest);
    if (status != 0) {
        goto cleanup;
    }

    // Perform some validation of the toolchain, while we could do more intense
    // validation, just make sure the type is marked correctly for now.
    if (toolchainOut->manifest->type != CHEF_PACKAGE_TYPE_TOOLCHAIN) {
        VLOG_ERROR("toolchain", "%s is not a toolchain package\n", packageReference);
        errno = EINVAL;
        status = -1;
        goto cleanup;
    }

    // Find a matching toolchain configuration for the platform. This step is currently
    // optional and not required.
    for (size_t i = 0; i < toolchainOut->manifest->toolchain.targets_count; i++) {
        const struct chef_package_manifest_toolchain_target* target =
            &toolchainOut->manifest->toolchain.targets[i];

        if (strcmp(target->name, toolchainPlatform) == 0) {
            toolchainOut->target = target;
            break;
        }
    }

    // If the toolchain has no target for the given platform, we can either
    // fail or we can let the toolchain be used without a specific target for the platform.
    // sometimes project supply their own things. We warn so it's visible, but do not
    // stop execution. (For now)
    if (toolchainOut->target == NULL) {
        VLOG_WARNING("toolchain", "no matching target found for platform %s in package %s\n", toolchainPlatform, packageReference);
    }

    toolchainOut->unpack_path = strpathcombine(toolchainsRoot, name);
    toolchainOut->package_path = platform_strdup(packagePath);
    if (toolchainOut->unpack_path == NULL || toolchainOut->package_path == NULL) {
        status = -1;
        goto cleanup;
    }

    toolchainOut->package_name = name;
    name = NULL;
    status = 0;

cleanup:
    free(name);
    free(channel);
    free(version);
    if (status != 0) {
        chef_toolchain_destroy(toolchainOut);
    }
    return status;
}
