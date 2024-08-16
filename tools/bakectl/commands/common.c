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

#include <chef/platform.h>
#include <liboven.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int __initialize_oven_options(struct oven_initialize_options* options, char** envp)
{
    char buff[PATH_MAX];

    options->envp = (const char* const*)envp,
    
    options->target_architecture = getenv("CHEF_TARGET_ARCH");
    if (options->target_architecture == NULL) {
        options->target_architecture = CHEF_ARCHITECTURE_STR;
    }

    options->target_platform = getenv("CHEF_TARGET_PLATFORM");
    if (options->target_platform == NULL) {
        options->target_platform = CHEF_PLATFORM_STR;
    }

    // some paths are easy
    options->paths.project_root = "/chef/project";
    options->paths.source_root = "/chef/source";
    options->paths.toolchains_root = "/chef/toolchains";

    // others require a bit of concatanation
    snprintf(&buff[0], sizeof(buff), "/chef/build/%s/%s",
        options->target_platform, options->target_architecture
    );
    options->paths.build_root = strdup(&buff[0]);
    if (options->paths.build_root == NULL) {
        return -1;
    }

    snprintf(&buff[0], sizeof(buff), "/chef/ingredients/%s/%s",
        options->target_platform, options->target_architecture
    );
    options->paths.build_ingredients_root = strdup(&buff[0]);
    if (options->paths.build_ingredients_root == NULL) {
        return -1;
    }

    snprintf(&buff[0], sizeof(buff), "/chef/install/%s/%s",
        options->target_platform, options->target_architecture
    );
    options->paths.install_root = strdup(&buff[0]);
    if (options->paths.install_root == NULL) {
        return -1;
    }

    return 0;
}

void __destroy_oven_options(struct oven_initialize_options* options)
{
    free((void*)options->paths.build_root);
    free((void*)options->paths.build_ingredients_root);
    free((void*)options->paths.install_root);
}
