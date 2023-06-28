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

#ifndef __FRIDGE_STORE_H__
#define __FRIDGE_STORE_H__

#include <chef/platform.h>
#include <libfridge.h>

struct fridge_store;

/**
 * @brief Initializes and loads the store. This does not load the store inventory, and
 * any inventory operations, must be done by first _open'ing the store inventory and closing
 * it again when done, as it accesses a shared file.
 */
extern int fridge_store_load(const char* platform, const char* arch, struct fridge_store** storeOut);

/**
 * @brief Opens the store inventory. Must be done before any calls to ensure_ingredient.
 */
extern int fridge_store_open(struct fridge_store* store);

/**
 * @brief Saves any changes done to the inventory and closes the file, allowing access from other instances.
 */
extern int fridge_store_close(struct fridge_store* store);

/**
 * @brief Ensures the presence of the fridge_ingredient passed. If it doesn't exist in the store already, then
 * its automatically downloaded and added.
 */
extern int fridge_store_ensure_ingredient(struct fridge_store* store, struct fridge_ingredient* ingredient, struct fridge_inventory_pack** packOut);

#endif //!__FRIDGE_STORE_H__
