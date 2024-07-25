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

#ifndef __CHEF_RECIPE_H__
#define __CHEF_RECIPE_H__

#include <stddef.h>
#include <chef/package.h>
#include <chef/list.h>
#include <libfridge.h>
#include <liboven.h>

enum recipe_step_type {
    RECIPE_STEP_TYPE_UNKNOWN,
    RECIPE_STEP_TYPE_GENERATE,
    RECIPE_STEP_TYPE_BUILD,
    RECIPE_STEP_TYPE_SCRIPT,
};

struct recipe_step {
    struct list_item           list_header;
    const char*                name;
    enum recipe_step_type      type;
    const char*                system;
    const char*                script;
    struct list                depends;
    struct list                arguments;
    struct list                env_keypairs;
    union oven_backend_options options;
};

struct recipe_part {
    struct list_item list_header;
    const char*      name;
    const char*      path;
    const char*      toolchain;
    struct list      steps;
};

struct recipe_project {
    const char* name;
    const char* summary;
    const char* description;
    const char* icon;
    const char* version;
    const char* license;
    const char* eula;
    const char* author;
    const char* email;
    const char* url;
};

struct recipe_platform {
    struct list_item list_header;
    const char*      name;
    const char*      toolchain;
    struct list      archs;  // list<list_item_string>
};

enum recipe_ingredient_type {
    RECIPE_INGREDIENT_TYPE_HOST,
    RECIPE_INGREDIENT_TYPE_BUILD,
    RECIPE_INGREDIENT_TYPE_RUNTIME
};

struct recipe_ingredient {
    struct list_item            list_header;
    enum recipe_ingredient_type type;
    const char*                 name;
    const char*                 channel;
    const char*                 version;
    struct ingredient_source    source;
    struct list                 filters;  // list<list_item_string>
};

struct recipe_pack_ingredient_options {
    struct list bin_dirs;
    struct list inc_dirs;
    struct list lib_dirs;
    struct list compiler_flags;
    struct list linker_flags;
};

struct recipe_pack_command {
    struct list_item       list_header;
    const char*            name;
    const char*            description;
    const char*            icon;
    enum chef_command_type type;
    int                    allow_system_libraries;
    const char*            path;
    struct list            arguments; // list<list_item_string>
};

struct recipe_pack {
    struct list_item                      list_header;
    const char*                           name;
    enum chef_package_type                type;
    struct recipe_pack_ingredient_options options;
    struct list                           filters;  // list<list_item_string>
    struct list                           commands; // list<recipe_pack_command>
};

struct recipe_host_environment {
    int         base;
    struct list ingredients; // list<recipe_ingredient>

    // linux specific host options
    struct list packages; // list<list_item_string>
};

struct recipe_build_environment {
    int         confinement;
    struct list ingredients; // list<recipe_ingredient>
};

struct recipe_rt_environment {
    struct list ingredients; // list<recipe_ingredient>
};

struct recipe_environment_hooks {
    const char* bash;
    const char* powershell;
};

struct recipe_environment {
    struct recipe_host_environment  host;
    struct recipe_build_environment build;
    struct recipe_rt_environment    runtime;
    struct recipe_environment_hooks hooks;
};

struct recipe {
    struct recipe_project     project;
    struct list               platforms;   // list<recipe_platform>
    struct recipe_environment environment;
    struct list               parts;       // list<recipe_part>
    struct list               packs;       // list<recipe_pack>
};

/**
 * @brief Parses a recipe from a yaml file buffer.
 * 
 * @param[In]  buffer 
 * @param[In]  length 
 * @param[Out] recipeOut
 * @return int 
 */
extern int recipe_parse(void* buffer, size_t length, struct recipe** recipeOut);

/**
 * @brief Cleans up any resources allocated during recipe_parse, and frees the recipe.
 * 
 * @param[In] recipe 
 */
extern void recipe_destroy(struct recipe* recipe);


// recipe parser utilities
extern int recipe_parse_platform_toolchain(const char* toolchain, char** ingredient, char** channel, char** version);
extern const char* recipe_find_platform_toolchain(struct recipe* recipe, const char* platform);
extern int recipe_validate_target(struct recipe* recipe, const char** expectedPlatform, const char** expectedArch);
extern int recipe_parse_part_step(const char* str, char** part, char** step);

// recipe cache
enum recipe_cache_change_type {
    RECIPE_CACHE_CHANGE_ADDED,
    RECIPE_CACHE_CHANGE_UPDATED,
    RECIPE_CACHE_CHANGE_REMOVED
};

struct recipe_cache_package_change {
    enum recipe_cache_change_type type;
    const char*                   name;
};

extern int         recipe_cache_initialize(struct recipe* current, const char* cwd);
extern const char* recipe_cache_uuid(void);
extern const char* recipe_cache_uuid_for(const char* name);
extern void        recipe_cache_transaction_begin(void);
extern void        recipe_cache_transaction_commit(void);

extern int recipe_cache_calculate_package_changes(struct recipe_cache_package_change** changes, int* changeCount);
extern int recipe_cache_commit_package_changes(struct recipe_cache_package_change* changes, int count);

extern int recipe_cache_mark_step_complete(const char* part, const char* step);
extern int recipe_cache_mark_step_incomplete(const char* part, const char* step);
extern int recipe_cache_is_step_complete(const char* part, const char* step);

/**
 * @brief Clears all cache data for the given cache name.
 * @return 0 for success, non-zero for error.
 */
extern int recipe_cache_clear_for(const char* name);

/**
 * @brief Reads a string value from the recipe cache by the given key.
 * @return Non-null if the key was set, otherwise NULL.
 */
extern const char* recipe_cache_key_string(const char* key);

/**
 * @brief Writes a string value to the recipe cache under the given key.
 * Any pre-existing value for this key is overwritten.
 * @return 0 for success, non-zero for error.
 */
extern int recipe_cache_key_set_string(const char* key, const char* value);

/**
 * @brief Wrapper around recipe_cache_key_string that reads a boolean value
 * from the cache under the given key. 
 * @return 1 if key exists and was set to true, 0 otherwise.
 */
extern int recipe_cache_key_bool(const char* key);

/**
 * @brief Wrapper around recipe_cache_key_set_string that writes a boolean value
 * to the cache for the given key.
 * @return 0 if the key was set, non-zero for error.
 */
extern int recipe_cache_key_set_bool(const char* key, int value);

#endif //!__CHEF_RECIPE_H__
