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

#ifndef __LIBCVD_H__
#define __LIBCVD_H__

#include <chef/config.h>
#include <chef/recipe.h>
#include <gracht/client.h>
#include "chef_cvd_service.h"

struct build_cache;

struct __build_clean_options {
    // part_or_step can either reference a step in the format of '<part>/<step>'
    // or reference just a part in the format of '<part>'. If this is NULL, then
    // this will clean the entire recipe.
    const char* part_or_step;
};

struct __bake_build_options {
    struct recipe*              recipe;
    const char*                 target_architecture;
    const char*                 target_platform;
    const char*                 cwd;
    const char* const*          envp;
    const char*                 recipe_path;
    struct build_cache*         build_cache;
    struct chef_config_address* cvd_address;
};

struct __bake_build_context {
    struct recipe*      recipe;
    const char*         recipe_path;
    struct build_cache* build_cache;
    
    const char*         host_cwd;
    const char*         bakectl_path;
    const char*         rootfs_path;
    const char*         install_path; // inside <rootfs>
    const char*         build_ingredients_path; // inside <rootfs>

    const char*         target_architecture;
    const char*         target_platform;

    const char* const*         base_environment;
    struct chef_config_address cvd_address;
    gracht_client_t*           cvd_client;
    char*                      cvd_id;
};

extern struct __bake_build_context* build_context_create(struct __bake_build_options* options);

extern void build_context_destroy(struct __bake_build_context* bctx);

extern int bake_build_setup(struct __bake_build_context* bctx);

extern int build_step_source(struct __bake_build_context* bctx);

extern int build_step_make(struct __bake_build_context* bctx);

extern int build_step_pack(struct __bake_build_context* bctx);

extern int bake_step_clean(struct __bake_build_context* bctx, struct __build_clean_options* options);

extern int bake_purge_kitchens(void);

extern int bake_client_initialize(struct __bake_build_context* bctx);

extern enum chef_status bake_client_create_container(struct __bake_build_context* bctx, struct chef_container_mount* mounts, unsigned int count);

extern enum chef_status bake_client_spawn(struct __bake_build_context* bctx, const char* command, enum chef_spawn_options options, unsigned int* pidOut);

extern enum chef_status bake_client_upload(struct __bake_build_context* bctx, const char* hostPath, const char* containerPath);

extern enum chef_status bake_client_destroy_container(struct __bake_build_context* bctx);


extern int         build_cache_create(struct recipe* current, const char* cwd, struct build_cache** cacheOut);
extern int         build_cache_create_null(struct recipe* current, struct build_cache** cacheOut);
extern const char* build_cache_uuid(struct build_cache* cache);
extern const char* build_cache_uuid_for(struct build_cache* cache, const char* name);
extern void        build_cache_transaction_begin(struct build_cache* cache);
extern void        build_cache_transaction_commit(struct build_cache* cache);

extern int build_cache_mark_part_sourced(struct build_cache* cache, const char* part);
extern int build_cache_is_part_sourced(struct build_cache* cache, const char* part);

extern int build_cache_mark_step_complete(struct build_cache* cache, const char* part, const char* step);
extern int build_cache_mark_step_incomplete(struct build_cache* cache, const char* part, const char* step);
extern int build_cache_is_step_complete(struct build_cache* cache, const char* part, const char* step);

/**
 * @brief Clears all cache data for the given cache name.
 * @return 0 for success, non-zero for error.
 */
extern int build_cache_clear_for(struct build_cache* cache, const char* name);

/**
 * @brief Reads a string value from the recipe cache by the given key.
 * @return Non-null if the key was set, otherwise NULL.
 */
extern const char* build_cache_key_string(struct build_cache* cache, const char* key);

/**
 * @brief Writes a string value to the recipe cache under the given key.
 * Any pre-existing value for this key is overwritten.
 * @return 0 for success, non-zero for error.
 */
extern int build_cache_key_set_string(struct build_cache* cache, const char* key, const char* value);

/**
 * @brief Wrapper around build_cache_key_string that reads a boolean value
 * from the cache under the given key. 
 * @return 1 if key exists and was set to true, 0 otherwise.
 */
extern int build_cache_key_bool(struct build_cache* cache, const char* key);

/**
 * @brief Wrapper around build_cache_key_set_string that writes a boolean value
 * to the cache for the given key.
 * @return 0 if the key was set, non-zero for error.
 */
extern int build_cache_key_set_bool(struct build_cache* cache, const char* key, int value);

#endif //!__LIBCVD_H__
