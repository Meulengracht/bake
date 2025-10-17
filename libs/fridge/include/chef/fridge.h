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

struct fridge_package {
    const char* name;
    const char* channel;
    const char* version;
    const char* platform;
    const char* arch;
};

struct fridge_store_backend {
    int (*resolve_package)(const char* publisher, const char* package, const char* platform, const char* arch, const char* channel, const char* path, int* revisionDownloaded);
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
 * @brief Stores the given package, making sure we have a local copy of it in
 * our local store.
 * 
 * @param[In] package Options describing the package that should be fetched from store.
 * @return int 
 */
extern int fridge_ensure_package(struct fridge_package* package);

/**
 * @brief Retrieves the path of an package based on it's parameters. It must be already
 * present in the local store.
 * 
 * @param[In]  package Options describing the package from the store.
 * @param[Out] pathOut Returns a zero-terminated string with the path of the package. This
 *                     string should not be freed. It will be valid until fridge_cleanup is called.
 * @return int  
 */
extern int fridge_package_path(struct fridge_package* package, const char** pathOut);

/**
 * @brief Retrieves the path of an package based on it's parameters. It must be already
 * present in the local store.
 * 
 * @param[In]  package Options describing the package from the store.
 * @param[Out] pathOut Returns a zero-terminated string with the path of the package. This
 *                     string should not be freed. It will be valid until fridge_cleanup is called.
 * @return int  
 */
extern int fridge_package_proof(struct fridge_package* package, const char** pathOut);

#endif //!__LIBFRIDGE_H__
