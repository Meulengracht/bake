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

#ifndef __LIBCHEF_API_PACKAGE_SETTINGS_H__
#define __LIBCHEF_API_PACKAGE_SETTINGS_H__

#include <stddef.h>

struct chef_package_settings;

struct chef_settings_params {
    const char* package;
};

/**
 * @brief Retrieves the package settings information. This requires
 * that @chefclient_login has been called.
 * 
 * @param[In] settingsOut A pointer where to store the allocated package settings instance.
 * @return int -1 on error, 0 on success. Errno will be set accordingly.
 */
extern int chefclient_pack_settings_get(struct chef_settings_params* params, struct chef_package_settings** settingsOut);

/**
 * @brief 
 * 
 * @param params 
 * @param path 
 * @return int 
 */
extern int chefclient_pack_settings_update(struct chef_package_settings* settings);

/**
 * @brief Creates a new package settings instance. This returns an empty malloc'd instance
 * of chef_package_settings. This instance must be freed with chef_package_settings_delete.
 */
extern struct chef_package_settings* chef_package_settings_new(void);

/**
 * @brief Cleans up any resources allocated by either @chefclient_pack_settings_get or @chef_package_settings_new.
 * 
 * @param[In] settings A pointer to the package settings that will be freed. 
 */
extern void chef_package_settings_delete(struct chef_package_settings* settings);

extern int  chef_package_settings_get_discoverable(struct chef_package_settings* settings);
extern void chef_package_settings_set_discoverable(struct chef_package_settings* settings, int discoverable);

#endif //!__LIBCHEF_API_PACKAGE_SETTINGS_H__
