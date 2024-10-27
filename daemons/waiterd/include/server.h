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

#ifndef __WAITERD_PRIVATE_H__
#define __WAITERD_PRIVATE_H__

#include <gracht/server.h>
#include <chef/platform.h>

enum waiterd_architecture {
    WAITERD_ARCHITECTURE_X86 = 0x1,
    WAITERD_ARCHITECTURE_X64 = 0x2,
    WAITERD_ARCHITECTURE_ARMHF = 0x4,
    WAITERD_ARCHITECTURE_ARM64 = 0x10,
    WAITERD_ARCHITECTURE_RISCV64 = 0x20
};

enum waiterd_build_status {
    WAITERD_BUILD_STATUS_UNKNOWN,
    WAITERD_BUILD_STATUS_QUEUED,
    WAITERD_BUILD_STATUS_SOURCING,
    WAITERD_BUILD_STATUS_BUILDING,
    WAITERD_BUILD_STATUS_PACKING,
    WAITERD_BUILD_STATUS_DONE,
    WAITERD_BUILD_STATUS_FAILED
};

struct waiterd_cook {
    struct list_item          list_header;
    gracht_conn_t             client;
    int                       ready;
    enum waiterd_architecture architectures;
};

struct waiterd_request {
    struct list_item       list_header;
    struct gracht_message* source;
    gracht_conn_t          cook;

    char                      guid[40];
    enum waiterd_architecture architecture;
    enum waiterd_build_status status;

    struct {
        char* package;
        char* log;
    } artifacts;
};

struct waiterd_config_address {
    const char*    type;
    const char*    address;
    unsigned short port;
};

struct waiterd_server {
    struct list cooks; // list<waiterd_cook>
    struct list requests; // list<waiterd_request>
};

extern int waiterd_config_load(const char* confdir);
extern void waiterd_config_api_address(struct waiterd_config_address* address);
extern void waiterd_config_cook_address(struct waiterd_config_address* address);

// callbacks for the server
extern void waiterd_server_cook_connect(gracht_conn_t client);
extern void waiterd_server_cook_disconnect(gracht_conn_t client);

// for now keep this here, if this daemon gets bigger let us 
// actually add some proper structure
extern int waiterd_initialize_server(struct gracht_server_configuration* config, gracht_server_t** serverOut);

/**
 * @brief Marks a cook as ready
 * 
 * @param client 
 * @param arch 
 */
extern void waiterd_server_cook_ready(gracht_conn_t client, enum waiterd_architecture arch);

/**
 * @brief Finds a cook based on the architecture, internal load-balancing
 * may occur
 */
extern struct waiterd_cook* waiterd_server_cook_find(enum waiterd_architecture arch);

/**
 * @brief
 */
extern struct waiterd_request* waiterd_server_request_new(
    struct waiterd_cook*   cook,
    struct gracht_message* message);

/**
 * @brief
 */
extern struct waiterd_request* waiterd_server_request_find(const char* id);

#endif //!__WAITERD_PRIVATE_H__
