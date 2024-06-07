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

// imports that don't really need to be exposed
struct pkgmngr;

struct kitchen_ingredient {
    struct list_item list_header;
    const char*      name;
    const char*      path;
};

struct kitchen_setup_hook {
    const char* bash;
    const char* powershell;
};

struct kitchen_setup_options {
    const char*        name;
    const char*        project_path;
    const char*        pkg_environment;
    int                confined;
    const char* const* envp;
    const char*        target_platform;
    const char*        target_architecture;
    struct list        host_ingredients; // list<kitchen_ingredient>
    struct list        build_ingredients; // list<kitchen_ingredient>
    struct list        runtime_ingredients; // list<kitchen_ingredient>

    // supported hooks during setup
    struct kitchen_setup_hook setup_hook;

    // linux specifics
    struct list* packages; // list<oven_value_item>
};

struct kitchen_purge_options {
    // TODO
};

struct kitchen_recipe_purge_options {
    // TODO
};

struct kitchen {
    // internal state
    int          original_root_fd;
    int          confined;
    unsigned int hash;

    char* target_platform;
    char* target_architecture;
    char* real_project_path;
    char* shared_output_path;

    struct pkgmngr* pkg_manager;

    // external paths that point inside chroot
    // i.e paths valid outside chroot
    char* host_chroot;
    char* host_build_path;
    char* host_build_ingredients_path;
    char* host_build_toolchains_path;
    char* host_project_path;
    char* host_install_root;
    char* host_install_path;
    char* host_checkpoint_path;
    char* host_hash_file;

    // internal paths
    // i.e paths valid during chroot
    char* project_root;
    char* build_root;
    char* build_ingredients_path;
    char* build_toolchains_path;
    char* install_root;
    char* install_path;
    char* checkpoint_root;
};

/**
 * @brief 
 * 
 * @param options 
 * @param kitchen 
 * @return int 
 */
extern int kitchen_setup(struct kitchen_setup_options* options, struct kitchen* kitchen);

/**
 * @brief 
 * 
 * @param options 
 * @return int 
 */
extern int kitchen_purge(struct kitchen_purge_options* options);

/**
 * @brief 
 * 
 * @param kitchen 
 * @param recipe 
 * @return int 
 */
extern int kitchen_recipe_make(struct kitchen* kitchen, struct recipe* recipe);

/**
 * @brief 
 * 
 * @param kitchen 
 * @param recipe 
 * @return int 
 */
extern int kitchen_recipe_pack(struct kitchen* kitchen, struct recipe* recipe);

/**
 * @brief 
 * 
 * @param kitchen 
 * @param recipe 
 * @param stepType 
 * @return int 
 */
extern int kitchen_recipe_clean(struct recipe* recipe);

/**
 * @brief Cleans up the build and install areas, resetting the entire state
 * of the current project context.
 * 
 * @return int Returns 0 on success, -1 on failure with errno set accordingly.
 */
extern int kitchen_recipe_purge(struct recipe* recipe, struct kitchen_recipe_purge_options* options);

#endif //!__CHEF_KITCHEN_H__
