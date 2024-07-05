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
#include <chef/recipe.h>
#include <liboven.h>
#include <stdlib.h>
#include <string.h>

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
    return platform_strdup(toolchain);
}

void oven_recipe_options_construct(struct oven_recipe_options* options, struct recipe_part* part, const char* toolchain)
{
    options->name          = part->name;
    options->relative_path = part->path;
    options->toolchain     = toolchain;
}
