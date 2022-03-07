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

struct oven_recipe_options {
    const char* name;
    const char* relative_path;
};

struct oven_generate_options {
    const char*  system;
    struct list* arguments;
    struct list* environment;
};

struct oven_build_options {
    const char*  system;
    struct list* arguments;
    struct list* environment;
};

struct oven_pack_options {
    const char* name;
    const char* description;
    const char* version;
    const char* license;
    const char* author;
    const char* email;
    const char* url;
};

/**
 * @brief 
 * 
 * @return int 
 */
extern int oven_initialize(char** envp);

/**
 * @brief 
 * 
 * @param options 
 * @return int 
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
 * @return int 
 */
extern int oven_configure(struct oven_generate_options* options);

/**
 * @brief 
 * 
 * @param options 
 * @return int 
 */
extern int oven_build(struct oven_build_options* options);

/**
 * @brief 
 * 
 * @param options 
 * @return int 
 */
extern int oven_pack(struct oven_pack_options* options);

/**
 * @brief 
 */
extern void oven_cleanup(void);

#endif //!__LIBOVEN_H__
