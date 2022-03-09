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

#ifndef __LIBCHEF_ACCOUNT_H__
#define __LIBCHEF_ACCOUNT_H__

#include <stddef.h>

struct chef_account;

/**
 * @brief Retrieves the account information of the current user. This requires
 * that @chefclient_login has been called.
 * 
 * @param[In] accountOut A pointer where to store the allocated account instance.
 * @return int -1 on error, 0 on success. Errno will be set accordingly.
 */
extern int chef_account_get(struct chef_account** accountOut);

/**
 * @brief 
 * 
 * @param account 
 * @return int 
 */
extern int chef_account_update(struct chef_account* account);

/**
 * @brief Creates a new account instance. This is neccessary if no account exists yet.
 * However the account is not saved to the server until chef_account_update is called.
 */
extern struct chef_account* chef_account_new(void);

/**
 * @brief Cleans up any resources allocated by either @chef_account_get or @chef_account_new.
 * 
 * @param[In] version A pointer to the account that will be freed. 
 */
extern void chef_account_free(struct chef_account* account);

extern const char* chef_account_get_publisher_name(struct chef_account* account);
extern void        chef_account_set_publisher_name(struct chef_account* account, const char* publisherName);

#endif //!__LIBCHEF_ACCOUNT_H__
