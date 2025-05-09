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

#ifndef __CVD_PRIVATE_H__
#define __CVD_PRIVATE_H__

#include <gracht/server.h>

struct cvd_config_address {
    const char*    type;
    const char*    address;
    unsigned short port;
};

/**
 * @brief
 */
extern int cvd_config_load(const char* confdir);

/**
 * @brief
 */
extern void cvd_config_destroy(void);

/**
 * @brief 
 */
extern void cvd_config_api_address(struct cvd_config_address* address);

/**
 * @brief
 */
extern int cvd_initialize_server(struct gracht_server_configuration* config, gracht_server_t** serverOut);

#endif //!__CVD_PRIVATE_H__
