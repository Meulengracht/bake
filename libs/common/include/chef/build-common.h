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
 * @brief Expand Chef variables and environment variables in a string.
 *
 * Chef variables use the $[[ NAME ]] form and are resolved through the
 * callback supplied by the caller. Environment variables use the $[ NAME ] form
 * and are read from the process environment. Whitespace immediately inside the
 * delimiters and leading ASCII spaces are ignored; all other text is copied unchanged.
 *
 * @param original Text to expand. The input is not modified.
 * @param resolve Callback used for Chef variables; it may be NULL when the
 *                 input contains only environment variables.
 * @param context Opaque value passed to @p resolve.
 * @return A newly allocated expanded string, or NULL on invalid input,
 *         allocation failure, or an unresolved variable.
 */
extern char* chef_preprocess_text(const char* original, const char* (*resolve)(const char*, void*), void* context);

/**
 * @brief 
 */
extern const char* chef_process_argument_list(struct list* argumentList, const char* (*resolve)(const char*, void*), void* context);

#endif //!__CHEF_COMMON_H__
