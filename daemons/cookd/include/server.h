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

/**
 * @brief 
 */
extern int cookd_server_init(gracht_client_t* client, int builderCount);

/**
 * @brief
 */
extern void cookd_server_cleanup(void);

struct cookd_status {
    int queue_size;
};

/**
 * @brief
 */
extern void cookd_server_status(struct cookd_status* status);

struct cookd_build_options {
    const char* platform;
    const char* architecture;
    const char* url;
    const char* recipe_path;
};

extern int cookd_server_queue_build(const char* id, struct cookd_build_options* options);

#endif //!__COOKD_SERVER_H__
