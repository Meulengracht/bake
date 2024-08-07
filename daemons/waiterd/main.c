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
#include "chef_waiterd_service_server.h"
#include "chef_waiterd_cook_service_server.h"

// the server object
static gracht_server_t* g_server = NULL;

int main(int argc, char** argv)
{
    struct gracht_server_configuration config;
    int                                status;

    // initialize the server configuration
    gracht_server_configuration_init(&config);
    
    // up the max size for protocols, due to the nature of transferring
    // git patches, we might need this for now
    gracht_server_configuration_set_max_msg_size(&config, 65*1024);

    // start up the server
    status = waiterd_initialize_server(&config, &g_server);

    // we register protocols
    gracht_server_register_protocol(g_server, &chef_waiterd_server_protocol);
    gracht_server_register_protocol(g_server, &chef_waiterd_cook_server_protocol);

    // use the default server loop
    return gracht_server_main_loop(g_server);
}
