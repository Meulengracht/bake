/**
 * Copyright 2022, Philip Meulengracht
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

#ifndef __BAKE_RECIPE_H__
#define __BAKE_RECIPE_H__

#include <stddef.h>
#include <chef/package.h>
#include <libfridge.h>
#include <liboven.h>
#include <list.h>

enum recipe_step_type {
    RECIPE_STEP_TYPE_UNKNOWN,
    RECIPE_STEP_TYPE_GENERATE,
    RECIPE_STEP_TYPE_BUILD,
};

struct recipe_step {
    struct list_item           list_header;
    enum recipe_step_type      type;
    const char*                system;
    struct list                depends;
    struct list                arguments;
    struct list                env_keypairs;
    union oven_backend_options options;
};

struct recipe_part {
    struct list_item list_header;
    const char*      name;
    const char*      path;
    const char*      toolchain;
    struct list      steps;
};

struct recipe_project {
    const char* summary;
    const char* description;
    const char* icon;
    const char* version;
    const char* license;
    const char* eula;
    const char* author;
    const char* email;
    const char* url;
};

struct recipe_ingredient {
    struct list_item         list_header;
    struct fridge_ingredient ingredient;
    int                      include;
    struct list              filters;  // list<oven_value_item>
};

struct recipe_pack {
    struct list_item       list_header;
    const char*            name;
    enum chef_package_type type;
    struct list            filters;  // list<oven_value_item>
    struct list            commands; // list<oven_pack_command>
};

struct recipe {
    struct recipe_project  project;
    struct list            ingredients; // list<recipe_ingredient>
    struct list            parts;       // list<recipe_part>
    struct list            packs;       // list<recipe_pack>
};

/**
 * @brief Parses a recipe from a yaml file buffer.
 * 
 * @param[In]  buffer 
 * @param[In]  length 
 * @param[Out] recipeOut
 * @return int 
 */
extern int recipe_parse(void* buffer, size_t length, struct recipe** recipeOut);

/**
 * @brief Cleans up any resources allocated during recipe_parse, and frees the recipe.
 * 
 * @param[In] recipe 
 */
extern void recipe_destroy(struct recipe* recipe);

#endif //!__BAKE_RECIPE_H__
