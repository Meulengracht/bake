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

#ifndef __LIBBAKE_H__
#define __LIBBAKE_H__

#include <chef/recipe.h>

struct recipe_cache;
struct pkgmngr;

struct __bakelib_context {
    struct recipe*       recipe;
    const char*          recipe_path;
    struct recipe_cache* cache;
    struct pkgmngr*      pkg_manager;

    const char*          build_platform;
    const char*          build_architecture;
    char**               build_environment;

    const char* build_directory;
    const char* build_ingredients_directory;
    const char* build_toolchains_directory;
    const char* install_directory;
};

/**
 * @brief
 */
extern struct __bakelib_context* __bakelib_context_new(
    struct recipe*     recipe,
    const char*        recipe_path,
    const char* const* envp);

/**
 * @brief
 */
extern void __bakelib_context_delete(struct __bakelib_context* context);

#endif //!__LIBBAKE_H__
