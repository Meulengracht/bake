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

extern int __chef_client_initialize(gracht_client_t** clientOut);

static void __print_help(void)
{
    printf("Usage: serve update [options]\n");
    printf("Options:\n");
    printf("  -h, --help\n");
    printf("      Print this help message\n");
}

int update_main(int argc, char** argv)
{
    gracht_client_t* client;
    int              status;

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

    gracht_client_shutdown(client);
    return status;
}
