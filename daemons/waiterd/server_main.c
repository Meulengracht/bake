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

#include "private.h"

static struct waiterd_server g_server = { 0 };


struct waiterd_server* waiterd_server_get(void)
{
    return &g_server;
}

void waiterd_server_cook_connect(gracht_conn_t client)
{
    // do nothing yet
}

void waiterd_server_cook_disconnect(gracht_conn_t client)
{
    // abort any request in flight for waiters

    // cleanup cook
}

void waiterd_server_cook_find()
{
    
}
