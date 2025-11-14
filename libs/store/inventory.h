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

#ifndef __CHEF_STORE_INVENTORY_H__
#define __CHEF_STORE_INVENTORY_H__

#include <chef/package.h>
#include <chef/store.h>
#include <time.h>

struct store_inventory_pack;
struct store_inventory;


/**
 * @brief Loads the inventory.json file from the path specified. The inventory
 * keeps state of what packs we keep in store, and when we last checked for new
 * versions
 * 
 * @param[In]  path         Path to the inventory.json
 * @param[Out] inventoryOut A pointer to a inventory pointer where the loaded storage will be allocated to.
 * @return int 0 on success, otherwise -1 and errno will be set
 */
extern int inventory_load(const char* path, struct store_inventory** inventoryOut);

/**
 * @brief Retrieves a given package matching the provided criteria from the inventory.
 * 
 * @param[In]  inventory The inventory instance to check.
 * @param[In]  publisher The publisher of the package.
 * @param[In]  package   The package from the publisher that we are adding.
 * @param[In]  platform  The platform target of the package.
 * @param[In]  arch      The platform architecture of the package.
 * @param[In]  channel   The channel of the package.
 * @param[In]  revision  The revision of the package.
 * @param[Out] packOut  A pointer to a pack pointer where the handle of the pack will be stored.
 * @return int 0 if the package is found, otherwise -1 and errno will be set accordingly.
 */
extern int inventory_get_pack(struct store_inventory* inventory, const char* publisher, 
    const char* package, const char* platform, const char* arch, const char* channel,
    int revision, struct store_inventory_pack** packOut);

/**
 * @brief Adds a new package to inventory
 * 
 * @param[In]  inventory The inventory instance the pack should be added to.
 * @param[In]  publisher The publisher of the package
 * @param[In]  package   The package from the publisher that we are adding.
 * @param[In]  platform  The platform target of the package.
 * @param[In]  arch      The platform architecture of the package.
 * @param[In]  channel   The channel of the package.
 * @param[In]  version   The revision of the package.
 * @param[Out] packOut   A pointer to a pack pointer where the handle of the pack will be stored.
 * @return int 0 on success, otherwise -1 and errno will be set
 */
extern int inventory_add(struct store_inventory* inventory, const char* packPath, const char* publisher,
    const char* package, const char* platform, const char* arch, const char* channel, int revision,
    struct store_inventory_pack** packOut);

extern int inventory_add_proof(struct store_inventory* inventory, union store_proof* proof);
extern int inventory_get_proof(struct store_inventory* inventory, enum store_proof_type keyType, const char* key, union store_proof* proof);
    
/**
 * @brief Saves the current inventory state.
 * 
 * @param[In] inventory The inventory to serialize.
 * @return int 0 on success, otherwise -1 and errno will be set
 */
extern int inventory_save(struct store_inventory* inventory);

/**
 * @brief Cleans up any resources allocated by the inventory_* functions.
 * 
 * @param[In] inventory The inventory to clean up. 
 */
extern void inventory_free(struct store_inventory* inventory);

/**
 * @brief Clears all items in the inventory.
 * 
 * @param[In] inventory The inventory to clear. 
 */
extern void inventory_clear(struct store_inventory* inventory);

// Retrieve information from the store_inventory_pack structure
extern const char* inventory_pack_name(struct store_inventory_pack* pack);
extern const char* inventory_pack_path(struct store_inventory_pack* pack);
extern const char* inventory_pack_platform(struct store_inventory_pack* pack);
extern const char* inventory_pack_arch(struct store_inventory_pack* pack);

#endif //!__CHEF_STORE_INVENTORY_H__
