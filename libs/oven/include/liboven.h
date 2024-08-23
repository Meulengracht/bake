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

#ifndef __LIBOVEN_H__
#define __LIBOVEN_H__

#include <chef/build-common.h>
#include <chef/package.h>
#include <chef/list.h>

//****************************************************************************//
// Oven backend options                                                       //
//****************************************************************************//
struct oven_ingredient {
    struct list_item     list_header;
    const char*          file_path;
    const char*          name;
    struct chef_version* version;
};

struct oven_recipe_options {
    const char* name;
    const char* toolchain;
};

struct oven_generate_options {
    const char*                 name;
    const char*                 profile;
    const char*                 system;
    union chef_backend_options* system_options;
    struct list*                arguments;
    struct list*                environment;
};

struct oven_build_options {
    const char*                 name;
    const char*                 profile;
    const char*                 system;
    union chef_backend_options* system_options;
    struct list*                arguments;
    struct list*                environment;
};

struct oven_clean_options {
    const char*  name;
    const char*  profile;
    const char*  system;
    struct list* arguments;
    struct list* environment;
};

struct oven_paths {
    const char* project_root;
    const char* source_root;
    const char* build_root;
    const char* install_root;
    const char* toolchains_root;
    const char* build_ingredients_root;
};

struct oven_initialize_options {
    const char* const* envp;
    const char*        target_platform;
    const char*        target_architecture;
    struct oven_paths  paths;
};

/**
 * @brief Initializes the oven system, creates all neccessary folders. All oven_*
 *       functions will fail if this function is not called first.
 * 
 * @param[In] parameters
 * @return int Returns 0 on success, -1 on failure with errno set accordingly.
 */
extern int oven_initialize(struct oven_initialize_options* parameters);

/**
 * @brief 
 * 
 * @param options 
 * @return int Returns 0 on success, -1 on failure with errno set accordingly.
 */
extern int oven_recipe_start(struct oven_recipe_options* options);

/**
 * @brief 
 * 
 */
extern void oven_recipe_end(void);

/**
 * @brief 
 * 
 * @param options 
 * @return int Returns 0 on success, -1 on failure with errno set accordingly.
 */
extern int oven_configure(struct oven_generate_options* options);

/**
 * @brief 
 * 
 * @param options 
 * @return int Returns 0 on success, -1 on failure with errno set accordingly.
 */
extern int oven_build(struct oven_build_options* options);

/**
 * @brief Runs a custom recipe script with default
 * 
 * @param options 
 * @return int Returns 0 on success, -1 on failure with errno set accordingly.
 */
extern int oven_script(const char* script);

/**
 * @brief Cleans the provided recipe part
 * 
 * @param options 
 * @return int Returns 0 on success, -1 on failure with errno set accordingly.
 */
extern int oven_clean(struct oven_clean_options* options);

/**
 * @brief 
 */
extern void oven_cleanup(void);

#endif //!__LIBOVEN_H__
