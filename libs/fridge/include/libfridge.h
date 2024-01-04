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

#ifndef __LIBFRIDGE_H__
#define __LIBFRIDGE_H__

#include <chef/list.h>

enum ingredient_source_type {
    INGREDIENT_SOURCE_TYPE_UNKNOWN,
    INGREDIENT_SOURCE_TYPE_REPO,
    INGREDIENT_SOURCE_TYPE_URL,
    INGREDIENT_SOURCE_TYPE_FILE,
};

struct ingredient_source_repo {
    const char* channel;
};

struct ingredient_source_url {
    const char* url;
};

struct ingredient_source_file {
    const char* path;
};

struct ingredient_source {
    enum ingredient_source_type type;
    union {
        struct ingredient_source_repo repo;
        struct ingredient_source_url  url;
        struct ingredient_source_file file;
    };
};

struct fridge_ingredient {
    const char*              name;
    const char*              channel;
    const char*              version;
    const char*              platform;
    const char*              arch;
    struct ingredient_source source;
};

/**
 * @brief 
 * 
 * @return int 
 */
extern int fridge_initialize(const char* platform, const char* architecture);

/**
 * @brief 
 */
extern void fridge_purge(void);

/**
 * @brief 
 */
extern void fridge_cleanup(void);

/**
 * @brief Stores the given ingredient, making sure we have a local copy of it in
 * our fridge storage.
 * 
 * @param[In] ingredient 
 * @return int  
 */
extern int fridge_store_ingredient(struct fridge_ingredient* ingredient);

/**
 * @brief Tells the fridge that we want to use a specific ingredient for our recipe. If
 * the ingredient doesn't exist, it will be fetched from chef.
 * 
 * @param[In] ingredient 
 * @return int  
 */
extern int fridge_use_ingredient(struct fridge_ingredient* ingredient);

/**
 * @brief Resolves the path of a utensils package. This will return the prefix path
 * that can be exposed to the building of the package.
 * 
 * @param[In] ingredient The name of the ingredient to resolve in the format of publisher/name
 * @return char* A malloc'd copy of the path prefix to the unpacked utensils package.
 */
extern char* fridge_get_utensil_location(const char* ingredient);

#endif //!__LIBFRIDGE_H__
