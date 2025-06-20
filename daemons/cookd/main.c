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
#include <server.h>
#include <vlog.h>

#include "chef-config.h"
#include "chef_waiterd_cook_service_client.h"
#include "private.h"

static void __print_help(void)
{
    printf("Usage: cookd [options]\n");
    printf("\n");
    printf("Options:\n");
    printf("  -v\n");
    printf("      Provide this for improved logging output\n");
    printf("  --version\n");
    printf("      Print the version of cookd\n");
    printf("  -h, --help\n");
    printf("      Print this help message\n");
}

int main(int argc, char** argv)
{
    gracht_client_t* client = NULL;
    int              status;
    int              logLevel = VLOG_LEVEL_TRACE;
    FILE*            debuglog;

    // parse options
    if (argc > 1) {
        for (int i = 1; i < argc; i++) {
            if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
                __print_help();
                return 0;
            } else if (!strcmp(argv[i], "--version")) {
                printf("cookd: version " PROJECT_VER "\n");
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

    // initialize directories
    status = chef_dirs_initialize(CHEF_DIR_SCOPE_DAEMON);
    if (status) {
        VLOG_ERROR("cookd", "failed to initialize directories\n");
        return -1;
    }

    // load config
    status = cookd_config_load(chef_dirs_config());
    if (status) {
        VLOG_ERROR("cookd", "failed to load configuration\n");
        return -1;
    }

    // add log file to vlog
    debuglog = chef_dirs_contemporary_file("cookd", "log", NULL);
    if (debuglog == NULL) {
        VLOG_ERROR("cookd", "failed to open log file\n");
        return -1;
    }
    vlog_add_output(debuglog, 1);
    vlog_set_output_level(debuglog, VLOG_LEVEL_DEBUG);

    // initialize the client
    status = cookd_initialize_client(&client);
    if (status) {
        VLOG_ERROR("cookd", "failed to initialize the client\n");
        goto cleanup;
    }

    // register protocols
    gracht_client_register_protocol(client, &chef_waiterd_cook_client_protocol);

    // initialize the server
    status = cookd_server_init(client, 1 /* needs to be configurable */);
    if (status) {
        VLOG_ERROR("cookd", "failed to initialize server subsystem\n");
        goto cleanup;
    }

    VLOG_TRACE("cookd", "registering with server\n");
    status = chef_waiterd_cook_ready(client, NULL, &(struct chef_cook_ready_event) { 
        .archs = CHEF_BUILD_ARCHITECTURE_X64 
    });

    VLOG_TRACE("cookd", "entering main message loop\n");
    for (;;) {
        gracht_client_wait_message(client, NULL, GRACHT_MESSAGE_BLOCK);
    }

cleanup:
    cookd_server_cleanup();
    gracht_client_shutdown(client);
    vlog_cleanup();
    cookd_config_destroy();
    return status;
}
