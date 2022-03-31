/**
 * Copyright 2022, Philip Meulengracht
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

#include <errno.h>
#include <gracht/client.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "chef_served_service_client.h"

extern int __chef_client_initialize(gracht_client_t** clientOut);

static void __print_help(void)
{
    printf("Usage: serve install <pack> [options]\n");
    printf("Options:\n");
    printf("  -h, --help\n");
    printf("      Print this help message\n");
}

int install_main(int argc, char** argv)
{
    gracht_client_t* client;
    int              status;
    const char*      package = NULL;

    if (argc > 2) {
        for (int i = 2; i < argc; i++) {
            if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
                __print_help();
                return 0;
            } else {
                if (package == NULL) {
                    package = argv[i];
                } else {
                    printf("serve: unknown option: %s\n", argv[i]);
                    __print_help();
                    return -1;
                }
            }
        }
    }

    if (package == NULL) {
        printf("serve: no package specified for install\n");
        __print_help();
        return -1;
    }

    status = __chef_client_initialize(&client);
    if (status != 0) {
        printf("serve: failed to initialize client: %s\n", strerror(status));
        return status;
    }

    printf("serve: installing package: %s... ", package);
    status = chef_served_install(client, NULL, package);
    if (status != 0) {
        printf("communication error: %i\n", status);
        return status;
    }

    gracht_client_wait_message(client, NULL, GRACHT_MESSAGE_BLOCK);
    gracht_client_shutdown(client);
    return status;
}

void chef_served_event_package_installed_invocation(gracht_client_t* client, const enum chef_install_status status, const struct chef_package* info)
{
    if (status != CHEF_INSTALL_STATUS_SUCCESS) {
        printf("failed: %s\n", strerror(status));
    } else {
        printf("done\n");
    }
}
