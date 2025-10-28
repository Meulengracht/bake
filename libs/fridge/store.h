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

#ifndef __FRIDGE_STORE_H__
#define __FRIDGE_STORE_H__

#include <chef/platform.h>
#include <chef/fridge.h>

struct fridge_store;

/**
 * @brief Initializes and loads the store. This does not load the store inventory, and
 * any inventory operations, must be done by first _open'ing the store inventory and closing
 * it again when done, as it accesses a shared file.
 */
extern int fridge_store_load(const char* platform, const char* arch, struct fridge_store_backend* backend, struct fridge_store** storeOut);

extern int fridge_store_download(
    struct fridge_store*   store,
    struct fridge_package* package,
    const char*            path,
    int*                   revisionDownloaded);

#endif //!__FRIDGE_STORE_H__
