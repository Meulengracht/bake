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

#ifndef __COOKD_SERVER_H__
#define __COOKD_SERVER_H__

#include <gracht/client.h>

struct cookd_config_address {
    const char*    type;
    const char*    address;
    unsigned short port;
};

/**
 * @brief
 */
extern int cookd_config_load(const char* confdir);

/**
 * @brief 
 */
extern void cookd_config_api_address(struct cookd_config_address* address);

/**
 * @brief
 */
extern int cookd_initialize_client(gracht_client_t** clientOut);

#endif //!__COOKD_SERVER_H__
