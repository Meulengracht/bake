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

#ifndef __CHEF_RECIPE_CACHE_H__
#define __CHEF_RECIPE_CACHE_H__

#include <chef/recipe.h>

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

struct recipe_cache;

extern int         recipe_cache_create(struct recipe* current, const char* cwd, struct recipe_cache** cacheOut);
extern int         recipe_cache_create_null(struct recipe* current, struct recipe_cache** cacheOut);
extern const char* recipe_cache_uuid(struct recipe_cache* cache);
extern const char* recipe_cache_uuid_for(struct recipe_cache* cache, const char* name);
extern void        recipe_cache_transaction_begin(struct recipe_cache* cache);
extern void        recipe_cache_transaction_commit(struct recipe_cache* cache);

extern int  recipe_cache_calculate_package_changes(struct recipe_cache* cache, struct recipe_cache_package_change** changes, int* changeCount);
extern int  recipe_cache_commit_package_changes(struct recipe_cache* cache, struct recipe_cache_package_change* changes, int count);
extern void recipe_cache_package_changes_destroy(struct recipe_cache_package_change* changes, int count);

extern int recipe_cache_mark_part_sourced(struct recipe_cache* cache, const char* part);
extern int recipe_cache_is_part_sourced(struct recipe_cache* cache, const char* part);

extern int recipe_cache_mark_step_complete(struct recipe_cache* cache, const char* part, const char* step);
extern int recipe_cache_mark_step_incomplete(struct recipe_cache* cache, const char* part, const char* step);
extern int recipe_cache_is_step_complete(struct recipe_cache* cache, const char* part, const char* step);

/**
 * @brief Clears all cache data for the given cache name.
 * @return 0 for success, non-zero for error.
 */
extern int recipe_cache_clear_for(struct recipe_cache* cache, const char* name);

/**
 * @brief Reads a string value from the recipe cache by the given key.
 * @return Non-null if the key was set, otherwise NULL.
 */
extern const char* recipe_cache_key_string(struct recipe_cache* cache, const char* key);

/**
 * @brief Writes a string value to the recipe cache under the given key.
 * Any pre-existing value for this key is overwritten.
 * @return 0 for success, non-zero for error.
 */
extern int recipe_cache_key_set_string(struct recipe_cache* cache, const char* key, const char* value);

/**
 * @brief Wrapper around recipe_cache_key_string that reads a boolean value
 * from the cache under the given key. 
 * @return 1 if key exists and was set to true, 0 otherwise.
 */
extern int recipe_cache_key_bool(struct recipe_cache* cache, const char* key);

/**
 * @brief Wrapper around recipe_cache_key_set_string that writes a boolean value
 * to the cache for the given key.
 * @return 0 if the key was set, non-zero for error.
 */
extern int recipe_cache_key_set_bool(struct recipe_cache* cache, const char* key, int value);

#endif //!__CHEF_RECIPE_CACHE_H__
