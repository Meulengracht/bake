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

#ifndef __CHEF_KITCHEN_H__
#define __CHEF_KITCHEN_H__


struct kitchen_options {
    const char*  name;
    const char*  install_path;
    const char*  project_path;
    int          confined;
    struct list* ingredients; // list<oven_ingredient>
    struct list* imports; // list<packaging_import>
};

struct kitchen {
    // internal: original_root_fd
    int original_root_fd;
    // internal: confined
    int confined;

    char* host_chroot;
    char* host_target_ingredients_path;
    char* host_build_path;
    char* host_install_path;
    char* host_checkpoint_path;

    char* project_root;
    char* build_root;
    char* install_root;
    char* target_ingredients_path;
};

/**
 * @brief
 */
extern int kitchen_setup(struct kitchen_options* options, struct kitchen* kitchen);

/**
 * @brief
 */
extern int kitchen_enter(struct kitchen* kitchen);

/**
 * @brief 
 */
extern int kitchen_leave(struct kitchen* kitchen);

#endif //!__CHEF_KITCHEN_H__
