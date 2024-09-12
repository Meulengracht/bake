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

#include "chef-config.h"
#include <chef/dirs.h>
#include <server.h>
#include "chef_waiterd_service_server.h"
#include "chef_waiterd_cook_service_server.h"
#include <vlog.h>

// the server object
static gracht_server_t* g_server = NULL;

static void __print_help(void)
{
    printf("Usage: waiterd [options]\n");
    printf("\n");
    printf("Options:\n");
    printf("  -v\n");
    printf("      Provide this for improved logging output\n");
    printf("  --version\n");
    printf("      Print the version of waiterd\n");
    printf("  -h, --help\n");
    printf("      Print this help message\n");
}

int main(int argc, char** argv)
{
    struct gracht_server_configuration config;
    int                                status;
    int                                logLevel = VLOG_LEVEL_TRACE;

    // parse options
    if (argc > 1) {
        for (int i = 1; i < argc; i++) {
            if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
                __print_help();
                return 0;
            } else if (!strcmp(argv[i], "--version")) {
                printf("waiterd: version " PROJECT_VER "\n");
                return 0;
            } else if (!strncmp(argv[i], "-v", 2)) {
                int li = 1;
                while (argv[i][li++] == 'v') {
                    logLevel++;
                }
            }
        }
    }

    // initialize logging
    vlog_initialize((enum vlog_level)logLevel);
    atexit(vlog_cleanup);

    // initialize directories
    status = chef_dirs_initialize();
    if (status) {
        fprintf(stderr, "waiterd: failed to initialize directories\n");
        return -1;
    }

    // load config
    status = waiterd_config_load(chef_dirs_root());
    if (status) {
        fprintf(stderr, "waiterd: failed to load configuation\n");
        return -1;
    }

    // add log file to vlog

    // initialize the server configuration
    gracht_server_configuration_init(&config);

    // setup callbacks for the server to listen to cooks
    // that connect over the tcp socket
    config.callbacks.clientConnected = waiterd_server_cook_connect;
    config.callbacks.clientDisconnected = waiterd_server_cook_disconnect;
    
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
