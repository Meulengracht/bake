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
#include <stdint.h>

// imports that don't really need to be exposed
typedef struct gracht_client gracht_client_t;
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

struct kitchen_init_options {
    const char*          kitchen_root;
    struct recipe*       recipe;
    struct recipe_cache* recipe_cache;
    const char*          recipe_path;
    const char* const*   envp;
    const char*          project_path;
    const char*          pkg_environment;
    const char*          target_platform;
    const char*          target_architecture;
};

struct kitchen_cvd_address {
    const char*    type;
    const char*    address;
    unsigned short port;
};

struct kitchen_setup_options {
    // where the build container interface can be found
    struct kitchen_cvd_address cvd_address;

    struct list        host_ingredients; // list<kitchen_ingredient>
    struct list        build_ingredients; // list<kitchen_ingredient>
    struct list        runtime_ingredients; // list<kitchen_ingredient>

    // supported hooks during setup
    struct kitchen_setup_hook setup_hook;

    // linux specifics
    struct list* packages; // list<list_item_string>
};

struct kitchen_purge_options {
    // TODO
    int dummy;
};

struct kitchen_recipe_clean_options {
    // part_or_step can either reference a step in the format of '<part>/<step>'
    // or reference just a part in the format of '<part>'. If this is NULL, then
    // this will clean the entire recipe.
    const char* part_or_step;
};

struct kitchen {
    uint32_t             magic;
    struct recipe*       recipe;
    struct recipe_cache* recipe_cache;
    const char*          recipe_path;

    // The path into the kitchen data path on the
    // host side (there is no child side) where the data
    // of the container resides
    char* host_kitchen_project_data_root;

    char* host_cwd;
    char* target_platform;
    char* target_architecture;

    gracht_client_t* cvd_client;
    char*            cvd_id;
    struct pkgmngr*  pkg_manager;
    char**           base_environment;

    // external paths that point inside chroot
    // i.e paths valid outside chroot
    char* host_chroot;
    char* host_build_path;
    char* host_build_ingredients_path;
    char* host_build_toolchains_path;
    char* host_project_path;
    char* host_install_root;
    char* host_install_path;

    // internal paths
    // i.e paths valid during chroot
    char* project_root;
    char* build_root;
    char* build_ingredients_path;
    char* build_toolchains_path;
    char* install_root;
    char* install_path;
    char* bakectl_path;
};

#define __KITCHEN_IF_CACHE(_k, _f) if (_k->recipe_cache != NULL) { _f; }

/**
 * @brief 
 * 
 * @param options 
 * @param kitchen 
 * @return int 
 */
extern int kitchen_initialize(struct kitchen_init_options* options, struct kitchen* kitchen);

/**
 * @brief 
 * 
 * @param kitchen 
 * @param options 
 * @return int 
 */
extern int kitchen_setup(struct kitchen* kitchen, struct kitchen_setup_options* options);

/**
 * @brief 
 * 
 * @param kitchen 
 */
extern void kitchen_destroy(struct kitchen* kitchen);

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
extern int kitchen_recipe_source(struct kitchen* kitchen);

/**
 * @brief 
 * 
 * @param kitchen 
 * @param recipe 
 * @return int 
 */
extern int kitchen_recipe_make(struct kitchen* kitchen);

/**
 * @brief 
 * 
 * @param kitchen 
 * @param recipe 
 * @return int 
 */
extern int kitchen_recipe_pack(struct kitchen* kitchen);

/**
 * @brief 
 * 
 * @param kitchen 
 * @param recipe 
 * @param stepType 
 * @return int 
 */
extern int kitchen_recipe_clean(struct kitchen* kitchen, struct kitchen_recipe_clean_options* options);

#endif //!__CHEF_KITCHEN_H__
