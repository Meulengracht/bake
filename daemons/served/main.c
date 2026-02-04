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

#include <chef/dirs.h>
#include <errno.h>
#include <gracht/link/socket.h>
#include <gracht/server.h>
#include <runner.h>
#include <utils.h>
#include <signal.h>
#include <stdio.h>
#include <vlog.h>

// server protocol
#include "chef_served_service_server.h"
#include "startup.h"

static const char*      g_servedUnPath = "/tmp/served";
static gracht_server_t* g_server       = NULL;

#ifdef CHEF_ON_WINDOWS
#include <windows.h>
#include <ws2ipdef.h>

// Windows 10 Insider build 17063 ++ 
#include <afunix.h>

#define AF_LOCAL AF_UNIX
#else
#include <sys/un.h>
#endif

static void init_link_config(struct gracht_link_socket* link)
{
    struct sockaddr_un addr = { 0 };
    
    unlink(g_servedUnPath);
    addr.sun_family = AF_LOCAL;
    strncpy(addr.sun_path, g_servedUnPath, sizeof(addr.sun_path));
    addr.sun_path[sizeof(addr.sun_path) - 1] = '\0';
    
    gracht_link_socket_set_type(link, gracht_link_stream_based);
    gracht_link_socket_set_bind_address(link, (const struct sockaddr_storage*)&addr, sizeof(struct sockaddr_un));
    gracht_link_socket_set_listen(link, 1);
    gracht_link_socket_set_domain(link, AF_LOCAL);
}

int __register_server_links(gracht_server_t* server)
{
    struct gracht_link_socket* clientLink;
    int                        code;

    gracht_link_socket_create(&clientLink);
    init_link_config(clientLink);

    code = gracht_server_add_link(server, (struct gracht_link*)clientLink);
    if (code) {
        printf("__register_server_links failed to add link: %i (%i)\n", code, errno);
    }
    return code;
}

int __init_server(gracht_server_t** serverOut)
{
    struct gracht_server_configuration serverConfiguration;
    int                                code;
    
    gracht_server_configuration_init(&serverConfiguration);
    
    code = gracht_server_create(&serverConfiguration, serverOut);
    if (code) {
        printf("__init_server: error initializing server library %i\n", errno);
        return code;
    }

    return __register_server_links(*serverOut);
}

static void __cleanup_systems(int sig)
{
    (void)sig;
    VLOG_TRACE("served", "termination requested, cleaning up\n");
    exit(0);
}

int main(int argc, char** argv)
{
    int status;

#ifdef CHEF_ON_WINDOWS
    utils_path_set_root("C:\\");
#elif defined(CHEF_ON_LINUX)
    utils_path_set_root("/");
#endif

    // parse for --root option, and set the utils_path_set_root
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--root") == 0 && i + 1 < argc) {
            utils_path_set_root(argv[i + 1]);
            i++; // skip next argument as it's the root path
        }
    }

    // initialize logging as the first thing, we need output!
    // debug for now, change this to trace later
    vlog_initialize(VLOG_LEVEL_DEBUG);

    // catch CTRL-C
    signal(SIGINT, __cleanup_systems);

    // must register this first as we want it called last!
    atexit(vlog_cleanup);

    status = chef_dirs_initialize(CHEF_DIR_SCOPE_DAEMON);
    if (status) {
        VLOG_ERROR("served", "failed to initialize directory code\n", status);
        return status;
    }

    // Initialize the system (loads state from database)
    status = served_startup();
    if (status) {
        VLOG_ERROR("served", "served_startup failed with code %d\n", status);
        return status;
    }
    atexit(served_shutdown);

    // Start the transaction runner thread
    status = served_runner_start();
    if (status) {
        VLOG_ERROR("served", "served_runner_start failed with code %d\n", status);
        return status;
    }
    
    VLOG_DEBUG("served", "runner thread started successfully\n");

    // Initialize gracht server
    status = __init_server(&g_server);
    if (status) {
        VLOG_ERROR("served", "__init_server failed with code %d\n", status);
        return status;
    }

    // Register protocol handlers
    gracht_server_register_protocol(g_server, &chef_served_server_protocol);
    
    VLOG_DEBUG("served", "entering main loop\n");
    return gracht_server_main_loop(g_server);
}

gracht_server_t* served_gracht_server(void)
{
    return g_server;
}
