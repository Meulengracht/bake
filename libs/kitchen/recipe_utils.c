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

static int __add_string_to_list(const char* str, struct list* out)
{
    struct list_item_string* item = malloc(sizeof(struct list_item_string));
    if (item == NULL) {
        return -1;
    }
    item->value = str;
    list_add(out, &item->list_header);
    return 0;
}

static int __determine_recipe_target(struct recipe* recipe, const char** platformOverride, struct list* archOverrides)
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

    // if no arch constraints are set, then we can safely skip the next check.
    if (platform->archs.count == 0) {
        return 0;
    }

    // if there are archs specified, then check against the override
    // verify each arch override, in a nice O(n^m) fashion.
    list_foreach(archOverrides, i) {
        const char*       arch = ((struct list_item_string*)i)->value;
        struct list_item* j;
        int               resolved = 0;
        list_foreach(&platform->archs, j) {
            if (strcmp(((struct list_item_string*)j)->value, arch) == 0) {
                resolved = 1;
                break;
            }
        }

        if (!resolved) {
            VLOG_ERROR("recipe", "architecture target %s was not supported for target platform\n", arch);
            return -1;
        }
    }
    return 0;
}

int recipe_ensure_target(struct recipe* recipe, const char** expectedPlatform, struct list* expectedArchs)
{
    // if no archs are provided, we can immediately default to the
    // host arch, since we cannot guess a cross-compile target
    if (expectedArchs->count == 0) {
        int status = __add_string_to_list(CHEF_ARCHITECTURE_STR, expectedArchs);
        if (status) {
            VLOG_ERROR("recipe", "failed to allocate memory for architecture target\n");
            return status;
        }
    }

    // if no platform is set, we take the first, so do not immediately
    // override that here, even if not provided immediately.

    // next, let's check if there are any constraints provided by
    // recipe in terms of platform/arch setup
    if (recipe->platforms.count > 0) {
        return __determine_recipe_target(recipe, expectedPlatform, expectedArchs);
    }

    // if no platform is still not set, then we can default to host. this will
    // happen if no cross-compilation setup is set in the recipe.
    if (*expectedPlatform == NULL) {
        // no platform override, we default to host
        *expectedPlatform = CHEF_PLATFORM_STR;
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

static int __add_maybe_package(struct recipe* recipe, const char* package)
{
    struct list_item_string* pkg;
    struct list_item* i;

    list_foreach(&recipe->environment.host.packages, i) {
        pkg = (struct list_item_string*)i;
        if (!strcmp(pkg->value, package)) {
            return 0;
        }
    }

    // not found, add it
    pkg = calloc(1, sizeof(struct list_item_string));
    if (pkg == NULL) {
        return -1;
    }
    
    pkg->value = strdup(package);
    if (pkg->value == NULL) {
        free(pkg);
        return -1;
    }

    list_add(&recipe->environment.host.packages, &pkg->list_header);
    return 0;
}

static int __discover_implicit_packages(struct recipe* recipe)
{
    struct list_item* i, *j;
    int               status;

    // if we are using host ingredients then maybe not
    // discover implicitly?
    if (recipe->environment.host.ingredients.count > 0) {
        return 0;
    }

    // always add build-essential
    status = __add_maybe_package(recipe, "build-essential");
    if (status) {
        return -1;
    }

    // handle implicit packages for parts
    list_foreach(&recipe->parts, i) {
        struct recipe_part* part = (struct recipe_part*)i;

        // check source
        switch (part->source.type) {
            case RECIPE_PART_SOURCE_TYPE_GIT: {
                status = __add_maybe_package(recipe, "git");
                if (status) {
                    return -1;
                }
            } break;

            default:
                break;
        }

        // handle implicit packages for steps
        list_foreach(&part->steps, j) {
            struct recipe_step* step = (struct recipe_step*)j;

            if (!strcmp(step->system, "cmake")) {
                status = __add_maybe_package(recipe, "cmake");
                if (status) {
                    return -1;
                }
            } else if (!strcmp(step->system, "ninja")) {
                status = __add_maybe_package(recipe, "ninja-build");
                if (status) {
                    return -1;
                }
            } else if (!strcmp(step->system, "meson")) {
                status = __add_maybe_package(recipe, "meson");
                if (status) {
                    return -1;
                }
            }
        }
    }
    return 0;
}

int recipe_postprocess(struct recipe* recipe)
{
    int status;

    status = __discover_implicit_packages(recipe);
    if (status) {
        return status;
    }

    return 0;
}
