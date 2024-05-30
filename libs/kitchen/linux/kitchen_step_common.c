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

#include <chef/kitchen.h>
#include <errno.h>
#include <fcntl.h>
#include "steps.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <vlog.h>

int kitchen_cooking_start(struct kitchen* kitchen)
{
    VLOG_DEBUG("kitchen", "kitchen_cooking_start(confined=%i)\n", kitchen->confined);
    
    if (!kitchen->confined) {
        // for an unconfined we do not chroot, instead we allow full access
        // to the base operating system to allow the the part to include all
        // it needs.
        return 0;
    }

    if (kitchen->original_root_fd > 0) {
        VLOG_ERROR("kitchen", "kitchen_enter: cannot recursively enter kitchen root\n");
        return -1;
    }

    kitchen->original_root_fd = open("/", __O_PATH);
    if (kitchen->original_root_fd < 0) {
        VLOG_ERROR("kitchen", "kitchen_enter: failed to get a handle on root: %s\n", strerror(errno));
        return -1;
    }

    if (chroot(kitchen->host_chroot)) {
        VLOG_ERROR("kitchen", "kitchen_enter: failed to change root environment to %s\n", kitchen->host_chroot);
        return -1;
    }

    // Change working directory to the known project root
    if (chdir(kitchen->project_root)) {
        VLOG_ERROR("kitchen", "kitchen_enter: failed to change working directory to %s\n", kitchen->project_root);
        return -1;
    }
    return 0;
}

int kitchen_cooking_end(struct kitchen* kitchen)
{
    VLOG_DEBUG("kitchen", "kitchen_cooking_end()\n");

    if (!kitchen->confined) {
        // nothing to do for unconfined
        return 0;
    }
    
    if (kitchen->original_root_fd <= 0) {
        return -1;
    }

    if (fchdir(kitchen->original_root_fd)) {
        return -1;
    }
    if (chroot(".")) {
        return -1;
    }
    close(kitchen->original_root_fd);
    kitchen->original_root_fd = 0;
    return 0;
}

char* kitchen_toolchain_resolve(struct recipe* recipe, const char* toolchain, const char* platform)
{
    if (strcmp(toolchain, "platform") == 0) {
        const char* fullChain = recipe_find_platform_toolchain(recipe, platform);
        char*       name;
        char*       channel;
        char*       version;
        if (fullChain == NULL) {
            return NULL;
        }
        if (recipe_parse_platform_toolchain(fullChain, &name, &channel, &version)) {
            return NULL;
        }
        free(channel);
        free(version);
        return name;
    }
    return strdup(toolchain);
}

void oven_recipe_options_construct(struct oven_recipe_options* options, struct recipe_part* part, const char* toolchain)
{
    options->name          = part->name;
    options->relative_path = part->path;
    options->toolchain     = toolchain;
}
