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

#ifndef __CHEF_RECIPE_H__
#define __CHEF_RECIPE_H__

#include <chef/build-common.h>
#include <chef/package.h>
#include <chef/list.h>
#include <stddef.h>

enum recipe_part_source_type {
    RECIPE_PART_SOURCE_TYPE_PATH,
    RECIPE_PART_SOURCE_TYPE_GIT,
    RECIPE_PART_SOURCE_TYPE_URL,
};

struct recipe_part_source_path {
    const char* path;
};

struct recipe_part_source_git {
    const char* url;

    // if either of these is not provided, build from
    // the default branch
    const char* branch;
    const char* commit;
};

struct recipe_part_source_url {
    const char* url;
};

struct recipe_part_source {
    enum recipe_part_source_type type;
    const char*                  script;
    union {
        struct recipe_part_source_path path;
        struct recipe_part_source_url  url;
        struct recipe_part_source_git  git;
    };
};

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
    union chef_backend_options options;
};

struct recipe_part {
    struct list_item          list_header;
    const char*               name;
    struct recipe_part_source source;
    const char*               toolchain;
    struct list               steps;
};

struct recipe_project {
    const char* name;
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
    const char*      base;
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
    const char*            path;
    struct list            arguments; // list<list_item_string>
};

struct recipe_pack_application_options {
    const char* gateway;
    const char* dns;
};

struct recipe_pack {
    struct list_item                       list_header;
    const char*                            name;
    const char*                            summary;
    const char*                            description;
    const char*                            icon;
    enum chef_package_type                 type;
    struct recipe_pack_application_options app_options;
    struct recipe_pack_ingredient_options  options;
    struct list                            filters;  // list<list_item_string>
    struct list                            commands; // list<recipe_pack_command>
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
    const char* setup;
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
extern int         recipe_parse_platform_toolchain(const char* toolchain, char** ingredient, char** channel, char** version);
extern const char* recipe_find_platform_toolchain(struct recipe* recipe, const char* platform);
extern int         recipe_ensure_target(struct recipe* recipe, const char** expectedPlatform, struct list* expectedArchs);
extern int         recipe_parse_part_step(const char* str, char** part, char** step);
extern const char* recipe_platform_base(struct recipe* recipe, const char* platform);

#endif //!__CHEF_RECIPE_H__
