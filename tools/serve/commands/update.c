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

static uint32_t g_updateCount = 0;

static void __print_help(void)
{
    printf("Usage: serve update [options]\n");
    printf("Options:\n");
    printf("  -p, --pack <packname>\n");
    printf("      If this option is provided, then only the provided pack will be updateds\n");
    printf("  -h, --help\n");
    printf("      Print this help message\n");
}

int update_main(int argc, char** argv)
{
    gracht_client_t*              client;
    struct gracht_message_context context;
    int                           status;
    const char*                   package = NULL;

    if (argc > 2) {
        for (int i = 2; i < argc; i++) {
            if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
                __print_help();
                return 0;
            } else if (!strcmp(argv[i], "-p") || !strcmp(argv[i], "--pack")) {
                package = argv[++i];
            } else {
                printf("serve: unknown option: %s\n", argv[i]);
                __print_help();
                return -1;
            }
        }
    }

    status = __chef_client_initialize(&client);
    if (status != 0) {
        printf("serve: failed to initialize client: %s\n", strerror(status));
        return status;
    }
    
    if (package != NULL) {
        printf("serve: updating package: %s\n", package);
        g_updateCount = 1;
        // TODO
    } else {
        // get the number of packages available
        status = chef_served_listcount(client, &context);
        if (status != 0) {
            printf("serve: failed to get package count: %s\n", strerror(status));
            return status;
        }
        gracht_client_wait_message(client, &context, GRACHT_MESSAGE_BLOCK);
        chef_served_listcount_result(client, &context, &g_updateCount);

        printf("serve: updating %i packages\n", g_updateCount);
        // TODO
    }

    // handle messages untill all requested updates are done
    while (g_updateCount != 0) {
        gracht_client_wait_message(client, NULL, GRACHT_MESSAGE_BLOCK);
    }

    gracht_client_shutdown(client);
    return status;
}

void chef_served_event_package_updated_invocation(gracht_client_t* client, const enum chef_update_status status, const struct chef_served_package* info)
{
    if (status == CHEF_UPDATE_STATUS_SUCCESS) {
        printf("serve: package %s updated to version %s\n", info->name, info->version);
    } else {
        printf("serve: failed to update package %s\n", info->name);
    }
    g_updateCount--;
}
