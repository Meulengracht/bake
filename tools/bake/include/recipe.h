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
    struct list_item      list_header;
    enum recipe_step_type type;
    const char*           system;
    struct list           depends;
    struct list           arguments;
    struct list           env_keypairs;
};

struct recipe_part {
    struct list_item list_header;
    const char*      name;
    const char*      path;
    const char*      toolchain;
    struct list      steps;
};

struct recipe_project {
    const char* name;
    const char* description;
    const char* version;
    const char* license;
    const char* author;
    const char* email;
    const char* url;
};

struct recipe_ingredient {
    struct list_item         list_header;
    struct fridge_ingredient ingredient;
};

enum recipe_command_type {
    RECIPE_COMMAND_TYPE_UNKNOWN,
    RECIPE_COMMAND_TYPE_EXECUTABLE,
    RECIPE_COMMAND_TYPE_DAEMON
};

struct recipe_command {
    struct list_item         list_header;
    const char*              name;
    const char*              description;
    enum recipe_command_type type;
    const char*              path;
    struct list              arguments;
};

struct recipe {
    struct recipe_project  project;
    enum chef_package_type type;
    struct list            ingredients;
    struct list            parts;
    struct list            commands;
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
