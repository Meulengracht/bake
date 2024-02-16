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

#include <chef/list.h>
#include <chef/recipe.h>

struct kitchen_ingredient {
    struct list_item list_header;
    const char*      name;
    const char*      path;
};

struct kitchen_options {
    const char*  name;
    const char*  project_path;
    int          confined;
    struct list  host_ingredients; // list<kitchen_ingredient>
    struct list  build_ingredients; // list<kitchen_ingredient>
    struct list  runtime_ingredients; // list<kitchen_ingredient>
};

struct kitchen {
    // internal: original_root_fd
    int original_root_fd;
    // internal: confined
    int confined;

    // external paths that point inside chroot
    // i.e paths valid outside chroot
    char* host_chroot;
    char* host_build_path;
    char* host_build_ingredients_path;
    char* host_build_toolchains_path;
    char* host_project_path;
    char* host_install_path;
    char* host_checkpoint_path;

    // internal paths
    // i.e paths valid during chroot
    char* project_root;
    char* build_root;
    char* build_ingredients_path;
    char* build_toolchains_path;
    char* install_root;
};

/**
 * @brief
 */
extern int kitchen_setup(struct kitchen_options* options, struct kitchen* kitchen);

/**
 * @brief 
 * 
 * @param kitchen 
 * @param recipe 
 * @param stepType 
 * @return int 
 */
extern int kitchen_prepare_recipe(struct kitchen* kitchen, struct recipe* recipe, enum recipe_step_type stepType);

/**
 * @brief 
 * 
 * @param kitchen 
 * @param recipe 
 * @return int 
 */
extern int kitchen_make_recipe(struct kitchen* kitchen, struct recipe* recipe);

/**
 * @brief 
 * 
 * @param kitchen 
 * @param recipe 
 * @return int 
 */
extern int kitchen_make_packs(struct kitchen* kitchen, struct recipe* recipe);

/**
 * @brief
 */
extern int kitchen_enter(struct kitchen* kitchen);

/**
 * @brief 
 */
extern int kitchen_leave(struct kitchen* kitchen);

#endif //!__CHEF_KITCHEN_H__
