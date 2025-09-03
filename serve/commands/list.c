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

#include <errno.h>
#include <gracht/client.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "chef_served_service_client.h"

extern int __chef_client_initialize(gracht_client_t** clientOut);

static void __print_help(void)
{
    printf("Usage: serve list [options]\n");
    printf("Options:\n");
    printf("  -h, --help\n");
    printf("      Print this help message\n");
}

int list_main(int argc, char** argv)
{
    gracht_client_t*              client;
    struct gracht_message_context context;
    int                           status;
    uint32_t                      packageCount;
    struct chef_served_package*   packages;

    if (argc > 2) {
        for (int i = 2; i < argc; i++) {
            if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
                __print_help();
                return 0;
            }
        }
    }

    status = __chef_client_initialize(&client);
    if (status != 0) {
        printf("serve: failed to initialize client: %s\n", strerror(status));
        return status;
    }

    status = chef_served_listcount(client, &context);
    if (status != 0) {
        printf("serve: failed to get installed packages: %s\n", strerror(status));
        return status;
    }
    gracht_client_wait_message(client, &context, GRACHT_MESSAGE_BLOCK);
    chef_served_listcount_result(client, &context, &packageCount);
    
    if (packageCount == 0) {
        printf("serve: no packages installed\n");
        goto cleanup;
    }

    // allocate memory for the package array
    packages = malloc(sizeof(struct chef_served_package) * packageCount);
    if (packages == NULL) {
        printf("serve: failed to allocate memory for package array: %s\n", strerror(errno));
        status = -1;
        goto cleanup;
    }

    status = chef_served_list(client, &context);
    if (status != 0) {
        printf("serve: failed to list installed packs: %s\n", strerror(status));
        goto cleanup;
    }
    gracht_client_wait_message(client, &context, GRACHT_MESSAGE_BLOCK);
    chef_served_list_result(client, &context, packages, packageCount);

    for (uint32_t i = 0; i < packageCount; i++) {
        printf("%30.30s %s\n", packages[i].name, packages[i].version);
    }

cleanup:
    gracht_client_shutdown(client);
    return status;
}
