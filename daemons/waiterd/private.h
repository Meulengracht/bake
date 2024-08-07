/**
 * Copyright 2024, Philip Meulengracht
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

#ifndef __WAITERD_PRIVATE_H__
#define __WAITERD_PRIVATE_H__

#include <gracht/server.h>
#include <chef/platform.h>

struct waiterd_cook {

};

struct waiterd_server {
    struct list cooks; // list<waiterd_cook>
};

// for now keep this here, if this daemon gets bigger let us 
// actually add some proper structure
extern int waiterd_initialize_server(struct gracht_server_configuration* config, gracht_server_t** serverOut);

/**
 * @brief retrieve a pointer to the global server instance
 */
extern struct waiterd_server* waiterd_server_get(void);

#endif //!__WAITERD_PRIVATE_H__
