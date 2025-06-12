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

#ifndef __LIBFRIDGE_H__
#define __LIBFRIDGE_H__

#include <chef/list.h>

struct fridge_ingredient {
    const char* name;
    const char* channel;
    const char* version;
    const char* platform;
    const char* arch;
};

struct fridge_store_backend {
    int (*resolve_ingredient)(const char* publisher, const char* package, const char* platform, const char* arch, const char* channel, struct chef_version* version, const char* path, int* revisionDownloaded);
};

struct fridge_parameters {
    const char*                 platform;
    const char*                 architecture;
    struct fridge_store_backend backend;
};

/**
 * @brief 
 * 
 * @return int 
 */
extern int fridge_initialize(struct fridge_parameters* parameters);

/**
 * @brief 
 */
extern void fridge_cleanup(void);

/**
 * @brief Stores the given ingredient, making sure we have a local copy of it in
 * our local store.
 * 
 * @param[In] ingredient Options describing the ingredient that should be fetched from store.
 * @return int 
 */
extern int fridge_ensure_ingredient(struct fridge_ingredient* ingredient);

/**
 * @brief Retrieves the path of an ingredient based on it's parameters. It must be already
 * present in the local store.
 * 
 * @param[In]  ingredient Options describing the ingredient from the store.
 * @param[Out] pathOut    Returns a zero-terminated string with the path of the ingredient. This
 *                        string should not be freed. It will be valid until fridge_cleanup is called.
 * @return int  
 */
extern int fridge_ingredient_path(struct fridge_ingredient* ingredient, const char** pathOut);

#endif //!__LIBFRIDGE_H__
