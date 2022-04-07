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
#include <list.h>

struct oven_keypair_item {
    struct list_item list_header;
    const char*      key;
    const char*      value;
};

struct oven_value_item {
    struct list_item list_header;
    const char*      value;
};

struct oven_pack_command {
    struct list_item       list_header;
    const char*            name;
    const char*            description;
    enum chef_command_type type;
    const char*            path;
    struct list            arguments; // list<oven_value_item>
};

struct oven_recipe_options {
    const char* name;
    const char* relative_path;
    const char* toolchain;
};

struct oven_generate_options {
    const char*  profile;
    const char*  system;
    struct list* arguments;
    struct list* environment;
};

struct oven_build_options {
    const char*  profile;
    const char*  system;
    struct list* arguments;
    struct list* environment;
};

struct oven_pack_options {
    const char*            name;
    enum chef_package_type type;
    const char*            summary;
    const char*            description;
    const char*            icon;
    const char*            version;
    const char*            license;
    const char*            eula;
    const char*            author;
    const char*            email;
    const char*            url;

    struct list*           filters;  // list<oven_value_item>
    struct list*           commands; // list<oven_pack_command>
};

/**
 * @brief Initializes the oven system, creates all neccessary folders. All oven_*
 *       functions will fail if this function is not called first.
 * 
 * @param[In] envp
 * @param[In] recipeScope
 * @param[In] fridgePrepDirectory
 * @return int Returns 0 on success, -1 on failure with errno set accordingly.
 */
extern int oven_initialize(char** envp, const char* recipeScope, const char* fridgePrepDirectory);

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
 * @return int Returns 0 on success, -1 on failure with errno set accordingly.
 */
extern int oven_reset(void);

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
