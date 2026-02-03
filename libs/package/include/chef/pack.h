
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

#ifndef __PACK_H__
#define __PACK_H__

#include <chef/package.h>

struct __application_configuration_options {
    // Optional runtime network defaults for applications.
    // These are embedded into the package metadata and can be overridden by the runtime.
    const char* network_gateway;
    const char* network_dns;
};

struct __ingredient_configuration_options {
    struct list* bin_dirs; // list<list_item_string>
    struct list* inc_dirs; // list<list_item_string>
    struct list* lib_dirs; // list<list_item_string>
    struct list* compiler_flags; // list<list_item_string>
    struct list* linker_flags; // list<list_item_string>
};

struct __pack_options {
    const char*            name;
    const char*            output_dir;
    const char*            input_dir;
    const char*            platform;
    const char*            architecture;

    enum chef_package_type type;
    const char*            base;
    const char*            summary;
    const char*            description;
    const char*            icon;
    const char*            version;
    const char*            license;
    const char*            eula;
    const char*            maintainer;
    const char*            maintainer_email;
    const char*            homepage;

    struct list*           filters;  // list<list_item_string>
    struct list*           commands; // list<recipe_pack_command>

    // Ingredient configuration options
    struct __ingredient_configuration_options ingredient_config;

    // Package configuration options
    struct __application_configuration_options app_config;
};

/**
 * @brief 
 * 
 * @param options 
 * @return int Returns 0 on success, -1 on failure with errno set accordingly.
 */
extern int bake_pack(struct __pack_options* options);

#endif //!__PACK_H__
