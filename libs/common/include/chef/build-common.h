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

#ifndef __CHEF_COMMON_H__
#define __CHEF_COMMON_H__

#include <chef/list.h>

struct chef_backend_make_options {
    int in_tree;
    int parallel;
};

struct meson_wrap_item {
    struct list_item list_header;
    const char*      name;
    const char*      ingredient;
};

struct chef_backend_meson_options {
    const char* cross_file;
    struct list wraps; // list<meson_wrap_item>
};

union chef_backend_options {
    struct chef_backend_make_options  make;
    struct chef_backend_meson_options meson;
};

/**
 * @brief Processes text and replaces identifiers it encounters in the text of the
 * following syntax:
 * Variables: $[[ VARIABLE ]]
 * Environment Values: $[ ENVIRONMENT_KEY ]
 * 
 * For variables, the supplied 'resolve' function will be used to lookup the
 * the value based on the name of the variable. If an invalid variable is provided
 * the function should return NULL.
 * @param original The original text that should be processed
 * @param resolve  A callback function that will supply values based on the variable name
 * @param context  Context pointer supplied to the resolve callback
 * 
 * @return The processed text with variable and environment keys substituted. The text
 * is malloc'd and must be freed.
 */
extern char* chef_preprocess_text(const char* original, const char* (*resolve)(const char*, void*), void* context);

/**
 * @brief 
 */
extern const char* chef_process_argument_list(struct list* argumentList, const char* (*resolve)(const char*, void*), void* context);

#endif //!__CHEF_COMMON_H__
