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
#include <list.h>

struct recipe_string_value {
    struct list_item list_header;
    const char*      value;
};

struct recipe_env_keypair {
    struct list_item list_header;
    const char*      key;
    const char*      value;
};

enum recipe_type {
    RECIPE_TYPE_UNKNOWN,
    RECIPE_TYPE_LIBRARY,
    RECIPE_TYPE_APPLICATION
};

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

enum recipe_ingredient_source_type {
    RECIPE_INGREDIENT_SOURCE_TYPE_UNKNOWN,
    RECIPE_INGREDIENT_SOURCE_TYPE_REPO,
    RECIPE_INGREDIENT_SOURCE_TYPE_URL,
    RECIPE_INGREDIENT_SOURCE_TYPE_FILE,
};

struct recipe_ingredient_source_url {
    const char* url;
};

struct recipe_ingredient_source_file {
    const char* path;
};

struct recipe_ingredient {
    struct list_item list_header;
    const char*      name;
    const char*      version;
    const char*      description;
    enum recipe_ingredient_source_type type;
    union {
        struct recipe_ingredient_source_url  url;
        struct recipe_ingredient_source_file file;
    };
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
    struct recipe_project project;
    enum recipe_type      type;
    struct list           ingredients;
    struct list           parts;
    struct list           commands;
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
