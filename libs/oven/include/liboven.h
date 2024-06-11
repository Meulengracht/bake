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

#ifndef __LIBOVEN_H__
#define __LIBOVEN_H__

#include <chef/package.h>
#include <chef/list.h>


//****************************************************************************//
// Oven backend options                                                       //
//****************************************************************************//
struct oven_backend_make_options {
    int in_tree;
    int parallel;
};

struct meson_wrap_item {
    struct list_item list_header;
    const char*      name;
    const char*      ingredient;
};

struct oven_backend_meson_options {
    const char* cross_file;
    struct list wraps; // list<meson_wrap_item>
};

union oven_backend_options {
    struct oven_backend_make_options  make;
    struct oven_backend_meson_options meson;
};

struct oven_value_item {
    struct list_item list_header;
    const char*      value;
};

struct oven_pack_command {
    struct list_item       list_header;
    const char*            name;
    const char*            description;
    const char*            icon;
    enum chef_command_type type;
    int                    allow_system_libraries;
    const char*            path;
    struct list            arguments; // list<oven_value_item>
};

struct oven_ingredient {
    struct list_item     list_header;
    const char*          file_path;
    const char*          name;
    struct chef_version* version;
};

struct oven_recipe_options {
    const char* name;
    const char* relative_path;
    const char* toolchain;
};

struct oven_generate_options {
    const char*                 name;
    const char*                 profile;
    const char*                 system;
    union oven_backend_options* system_options;
    struct list*                arguments;
    struct list*                environment;
};

struct oven_build_options {
    const char*                 name;
    const char*                 profile;
    const char*                 system;
    union oven_backend_options* system_options;
    struct list*                arguments;
    struct list*                environment;
};

struct oven_script_options {
    const char* name;
    const char* script;
};

struct oven_pack_options {
    const char*            name;
    const char*            pack_dir;
    enum chef_package_type type;
    const char*            summary;
    const char*            description;
    const char*            icon;
    const char*            version;
    const char*            license;
    const char*            eula;
    const char*            maintainer;
    const char*            maintainer_email;
    const char*            homepage;

    struct list*           bin_dirs; // list<oven_value_item>
    struct list*           inc_dirs; // list<oven_value_item>
    struct list*           lib_dirs; // list<oven_value_item>
    struct list*           compiler_flags; // list<oven_value_item>
    struct list*           linker_flags; // list<oven_value_item>

    struct list*           filters;  // list<oven_value_item>
    struct list*           commands; // list<oven_pack_command>
};

struct oven_paths {
    const char* project_root;
    const char* build_root;
    const char* install_root;
    const char* toolchains_root;
    const char* build_ingredients_root;
};

struct oven_parameters {
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
extern int oven_initialize(struct oven_parameters* parameters);

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
extern int oven_script(struct oven_script_options* options);

/**
 * @brief List of filepath patterns that should be included in the install directory.
 * This will be applied to the fridge prep area directory where ingredients are stored used
 * for building. This is to support runtime dependencies for packs.
 * 
 * @param[In] filters List of struct oven_value_item containg filepath patterns.
 * @return int Returns 0 on success, -1 on failure with errno set accordingly.
 */
extern int oven_include_filters(struct list* filters);

/**
 * @brief 
 * 
 * @param options 
 * @return int Returns 0 on success, -1 on failure with errno set accordingly.
 */
extern int oven_pack(struct oven_pack_options* options);

/**
 * @brief 
 */
extern void oven_cleanup(void);

#endif //!__LIBOVEN_H__
