
/**
 * Copyright 2024, Philip Meulengracht
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

#ifndef __KITCHEN_PACK_H__
#define __KITCHEN_PACK_H__

#include <chef/package.h>

struct kitchen_pack_options {
    const char*            name;
    const char*            sysroot_dir;
    const char*            output_dir;
    const char*            input_dir;
    const char*            ingredients_root;
    const char*            platform;
    const char*            architecture;

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

    struct list*           bin_dirs; // list<list_item_string>
    struct list*           inc_dirs; // list<list_item_string>
    struct list*           lib_dirs; // list<list_item_string>
    struct list*           compiler_flags; // list<list_item_string>
    struct list*           linker_flags; // list<list_item_string>

    struct list*           filters;  // list<list_item_string>
    struct list*           commands; // list<recipe_pack_command>
};

struct pack_resolve_commands_options {
    const char* sysroot;
    const char* install_root;
    const char* ingredients_root;
    const char* platform;
    const char* architecture;
};

/**
 * @brief 
 * 
 * @param options 
 * @return int Returns 0 on success, -1 on failure with errno set accordingly.
 */
extern int kitchen_pack(struct kitchen_pack_options* options);

/**
 * @brief 
 * 
 * @param[In] commands 
 * @param[In] resolves
 * @return int 
 */
extern int pack_resolve_commands(struct list* commands, struct list* resolves, struct pack_resolve_commands_options* options);

/**
 * @brief 
 * 
 * @param resolves 
 */
extern void pack_resolve_destroy(struct list* resolves);

#endif //!__KITCHEN_PACK_H__
