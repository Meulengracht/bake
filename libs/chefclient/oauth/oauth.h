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

#ifndef __LIBCHEF_OAUTH_H__
#define __LIBCHEF_OAUTH_H__

enum oauth_flow_type {
    OAUTH_FLOW_DEVICECODE
};

struct token_context {
    const char* access_token;
    const char* refresh_token;
    const char* id_token;
    int         expires_in;
};

/**
 * @brief 
 * 
 * @param flowType 
 * @return int 
 */
extern int oauth_login(enum oauth_flow_type flowType);

/**
 * @brief 
 * 
 */
extern void oauth_logout(void);

/**
 * @brief 
 * 
 * @param curl 
 */
extern void oauth_set_authentication(void** headerlist);

#endif //!__LIBCHEF_OAUTH_H__
