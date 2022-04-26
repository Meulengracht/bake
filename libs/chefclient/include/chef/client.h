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

#ifndef __LIBCHEF_CLIENT_H__
#define __LIBCHEF_CLIENT_H__

enum chef_login_flow_type {
    CHEF_LOGIN_FLOW_TYPE_OAUTH2_DEVICECODE
};

/**
 * @brief Initializes the chef client library and enables communication with the chef api
 * 
 * @return int returns -1 on error, 0 on success
 */
extern int chefclient_initialize(void);

/**
 * @brief Cleans up the chef client library. 
 * 
 */
extern void chefclient_cleanup(void);

/**
 * @brief Initializes a new authentication session with the chef api. This is required
 * to use the 'publish' functionality. The rest of the methods are unprotected.
 * 
 * @return int 
 */
extern int chefclient_login(enum chef_login_flow_type flowType);

/**
 * @brief Terminates the current authentication session with the chef api.
 * 
 */
extern void chefclient_logout(void);

#endif //!__LIBCHEF_CLIENT_H__
