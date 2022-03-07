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

enum ingredient_source {
    INGREDIENT_SOURCE_UNKNOWN,
    INGREDIENT_SOURCE_REPO,
    INGREDIENT_SOURCE_URL,
    INGREDIENT_SOURCE_FILE,
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

struct fridge_ingredient {
    const char* name;
    const char* description;
    const char* channel;
    const char* version;

    enum ingredient_source source;
    union {
        struct ingredient_source_repo repo;
        struct ingredient_source_url  url;
        struct ingredient_source_file file;
    };
};

/**
 * @brief 
 * 
 * @return int 
 */
extern int fridge_initialize(void);

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

#endif //!__LIBFRIDGE_H__
