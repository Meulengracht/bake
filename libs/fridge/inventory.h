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

struct fridge_inventory_pack {
    const char*         publisher;
    const char*         package;
    const char*         platform;
    const char*         arch;
    const char*         channel;
    struct chef_version version;
    int                 latest;
};

struct fridge_inventory {
    struct timespec               last_check;
    struct fridge_inventory_pack* packs;
    int                           packs_count;
};


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
 * @brief Checks the inventory for a given package.
 * 
 * @param[In] inventory The inventory instance to check.
 * @param[In] publisher The publisher of the package.
 * @param[In] package   The package from the publisher that we are adding.
 * @param[In] platform  The platform target of the package.
 * @param[In] arch      The platform architecture of the package.
 * @param[In] channel   The channel of the package.
 * @param[In] version   The current version of the package.
 * @param[In] latest    Whether or not we want to have the latest version.
 * @return int 0 if the package is found, otherwise -1 and errno will be set to ENOENT
 */
extern int inventory_contains(struct fridge_inventory* inventory, const char* publisher, 
    const char* package, const char* platform, const char* arch, const char* channel,
    struct chef_version* version, int latest);

/**
 * @brief Adds a new package to inventory
 * 
 * @param[In] inventory The inventory instance the pack should be added to.
 * @param[In] publisher The publisher of the package
 * @param[In] package   The package from the publisher that we are adding.
 * @param[In] platform  The platform target of the package.
 * @param[In] arch      The platform architecture of the package.
 * @param[In] channel   The channel of the package.
 * @param[In] version   The current version of the package.
 * @param[In] latest    Whether or not we want to have the latest version
 * @return int 0 on success, otherwise -1 and errno will be set
 */
extern int inventory_add(struct fridge_inventory* inventory, const char* publisher,
    const char* package, const char* platform, const char* arch, const char* channel,
    struct chef_version* version, int latest);

/**
 * @brief Saves the inventory to the given file path. The file created is json.
 * 
 * @param[In] inventory The inventory to serialize.
 * @param[In] path      The path of the file the inventory should be stored to.
 * @return int 0 on success, otherwise -1 and errno will be set
 */
extern int inventory_save(struct fridge_inventory* inventory, const char* path);

#endif //!__LIBFRIDGE_INVENTORY_H__
