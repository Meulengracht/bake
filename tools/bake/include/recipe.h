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

enum recipe_type {
    RECIPE_TYPE_LIBRARY,
    RECIPE_TYPE_APPLICATION
};

struct recipe_step {
    const char* name;
    const char* depends;
};

struct recipe_step_generate {
    struct recipe_step base;
    const char*        system;
    const char*        arguments;
};

struct recipe_step_build {
    struct recipe_step base;
    const char*        system;
    const char*        arguments;
};

struct recipe {
    const char*         name;
    enum recipe_type    type;
    const char*         description;
    const char*         version;
    const char*         license;
    const char*         author;
    const char*         email;
    const char*         url;
    const char*         platform;
    const char*         arch;
    struct recipe_step* steps;
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


#endif //!__BAKE_RECIPE_H__
