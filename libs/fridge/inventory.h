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

#ifndef __LIBFRIDGE_INVENTORY_H__
#define __LIBFRIDGE_INVENTORY_H__

#include <chef/package.h>
#include <time.h>

struct fridge_inventory_pack;
struct fridge_inventory;


/**
 * @brief Loads the inventory.json file from the path specified. The inventory
 * keeps state of what packs we keep in store, and when we last checked for new
 * versions
 * 
 * @param[In]  path         Path to the inventory.json
 * @param[Out] inventoryOut A pointer to a inventory pointer where the loaded storage will be allocated to.
 * @return int 0 on success, otherwise -1 and errno will be set
 */
extern int inventory_load(const char* path, struct fridge_inventory** inventoryOut);

/**
 * @brief Retrieves a given package matching the provided criteria from the inventory.
 * 
 * @param[In]  inventory The inventory instance to check.
 * @param[In]  publisher The publisher of the package.
 * @param[In]  package   The package from the publisher that we are adding.
 * @param[In]  platform  The platform target of the package.
 * @param[In]  arch      The platform architecture of the package.
 * @param[In]  channel   The channel of the package.
 * @param[In]  version   The current version of the package.
 * @param[Out] packOut  A pointer to a pack pointer where the handle of the pack will be stored.
 * @return int 0 if the package is found, otherwise -1 and errno will be set accordingly.
 */
extern int inventory_get_pack(struct fridge_inventory* inventory, const char* publisher, 
    const char* package, const char* platform, const char* arch, const char* channel,
    struct chef_version* version, struct fridge_inventory_pack** packOut);

/**
 * @brief Adds a new package to inventory
 * 
 * @param[In]  inventory The inventory instance the pack should be added to.
 * @param[In]  publisher The publisher of the package
 * @param[In]  package   The package from the publisher that we are adding.
 * @param[In]  platform  The platform target of the package.
 * @param[In]  arch      The platform architecture of the package.
 * @param[In]  channel   The channel of the package.
 * @param[In]  version   The current version of the package.
 * @param[Out] packOut   A pointer to a pack pointer where the handle of the pack will be stored.
 * @return int 0 on success, otherwise -1 and errno will be set
 */
extern int inventory_add(struct fridge_inventory* inventory, const char* publisher,
    const char* package, const char* platform, const char* arch, const char* channel,
    struct chef_version* version, struct fridge_inventory_pack** packOut);

/**
 * @brief Saves the inventory to the given file path. The file created is json.
 * 
 * @param[In] inventory The inventory to serialize.
 * @param[In] path      The path of the file the inventory should be stored to.
 * @return int 0 on success, otherwise -1 and errno will be set
 */
extern int inventory_save(struct fridge_inventory* inventory, const char* path);

/**
 * @brief Cleans up any resources allocated by the inventory_* functions.
 * 
 * @param[In] inventory The inventory to clean up. 
 */
extern void inventory_free(struct fridge_inventory* inventory);

/**
 * @brief Marks a pack for being currently unpacked. This can be used to indicate whether
 * or not a pack has been prepared for usage. It will be automatically cleared when the pack
 * is updated.
 * 
 * @param[In] pack The pack to mark as unpacked. 
 */
extern void inventory_pack_set_unpacked(struct fridge_inventory_pack* pack);

/**
 * @brief Queries the current unpack status for the pack
 * 
 * @param[In] pack The pack to query.
 * @return int 1 if the pack is unpacked, otherwise 0.
 */
extern int inventory_pack_is_unpacked(struct fridge_inventory_pack* pack);

#endif //!__LIBFRIDGE_INVENTORY_H__
