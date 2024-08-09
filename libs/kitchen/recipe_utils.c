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

#include <ctype.h>
#include <chef/platform.h>
#include <chef/recipe.h>
#include <stdlib.h>
#include <string.h>
#include <vlog.h>

#define __MIN(a, b) (((a) < (b)) ? (a) : (b))

int recipe_parse_platform_toolchain(const char* toolchain, char** ingredient, char** channel, char** version)
{
    const char* split;
    char        name[128] = { 0 };
    char        verr[128] = { 0 };
    
    split = strchr(toolchain, '=');
    if (split == NULL) {
        *ingredient = platform_strdup(toolchain);
        *channel = platform_strdup("stable");
        *version = NULL;
        return 0;
    }

    strncpy(&name[0], toolchain, __MIN((split - toolchain), sizeof(name)));
    strncpy(&verr[0], split+1, sizeof(verr));

    *ingredient = platform_strdup(&name[0]);
    // If the first character is a digit, assume version, installing by version
    // always tracks stable
    if (isdigit(verr[0])) {
        *channel = platform_strdup("stable");
        *version = platform_strdup(&verr[0]);
    } else {
        *channel = platform_strdup(&verr[0]);
        *version = NULL;
    }
    return 0;
}

const char* recipe_find_platform_toolchain(struct recipe* recipe, const char* platform)
{
    struct recipe_platform* p = NULL;
    struct list_item*       i;

    list_foreach(&recipe->platforms, i) {
        if (strcmp(((struct recipe_platform*)i)->name, platform) == 0) {
            p = (struct recipe_platform*)i;
            break;
        }
    }

    if (p == NULL) {
        return NULL;
    }
    return p->toolchain;
}

static int __determine_recipe_target(struct recipe* recipe, const char** platformOverride, const char** archOverride)
{
    struct recipe_platform* platform = NULL;
    struct list_item*       i;

    if (*platformOverride != NULL) {
        // If there is a platform override, make sure it appears in the list
        list_foreach(&recipe->platforms, i) {
            if (strcmp(((struct recipe_platform*)i)->name, *platformOverride) == 0) {
                platform = (struct recipe_platform*)i;
                break;
            }
        }

        if (platform == NULL) {
            VLOG_ERROR("recipe", "%s is not a supported platform for build\n", *platformOverride);
            return -1;
        }
    } else {
        platform = (struct recipe_platform*)recipe->platforms.head;
        if (platform == NULL) {
            VLOG_ERROR("recipe", "no supported platform for build\n");
            return -1;
        }
        *platformOverride = platform->name;
    }

    // default to host architecture
    if (*archOverride == NULL) {
        *archOverride = CHEF_ARCHITECTURE_STR;
    }

    // if there are archs specified, then check against the override
    if (platform->archs.count == 0) {
        return 0;
    }

    list_foreach(&platform->archs, i) {
        if (strcmp(((struct list_item_string*)i)->value, *archOverride) == 0) {
            return 0;
        }
    }

    VLOG_ERROR("recipe", "architecture target %s was not supported for target platform, use -cc switch to select another\n", *archOverride);
    return -1;
}

int recipe_validate_target(struct recipe* recipe, const char** expectedPlatform, const char** expectedArch)
{
    // First of all, let's check if there are any constraints provided by
    // recipe in terms of platform/arch setup
    if (recipe->platforms.count > 0) {
        return __determine_recipe_target(recipe, expectedPlatform, expectedArch);
    }

    // No constraints, we simply just check overrides
    if (*expectedPlatform == NULL) {
        // no platform override, we default to host
        *expectedPlatform = CHEF_PLATFORM_STR;
    }
    if (*expectedArch == NULL) {
        // no arch override, we default to host
        *expectedArch = CHEF_ARCHITECTURE_STR;
    }
    return 0;
}

int recipe_parse_part_step(const char* str, char** part, char** step)
{
    char* split;

    if (str == NULL) {
        *part = NULL;
        *step = NULL;
        return 0;
    }

    split = strchr(str, '/');
    if (split == NULL) {
        *part = platform_strdup(str);
        if (*part == NULL) {
            return -1;
        }
        return 0;
    }
    *part = platform_strndup(str, (size_t)(split - str));
    *step = platform_strdup(split + 1);
    if (*part == NULL || *step == NULL) {
        free(*part);
        return -1;
    }
    return 0;
}
