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
#include <chef/client.h>
#include <stdio.h>
#include <string.h>

static void __print_help(void)
{
    printf("Usage: order account <command> [options]\n");
    printf("\n");
    printf("Commands:\n");
    printf("  whoami      shows information about the currently logged in user\n");
    printf("  set         sets a specific account parameter\n");
    printf("  get         retrieves the value of a specific account paramater\n");
    printf("\n");
    printf("Options:\n");
    printf("  -h, --help\n");
    printf("      Print this help message\n");
}

int account_main(int argc, char** argv)
{
    char* command = NULL;
    int   status;

    if (argc > 2) {
        for (int i = 2; i < argc; i++) {
            if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
                __print_help();
                return 0;
            }
            else {
                if (command != NULL) {
                    command = argv[i];
                }
            }
        }
    }

    if (command == NULL) {
        printf("order: no command was specified for 'account'\n");
        return -1;
    }

    // initialize chefclient
    status = chefclient_initialize();
    if (status != 0) {
        fprintf(stderr, "order: failed to initialize chefclient: %s\n", strerror(errno));
        return -1;
    }

    // login before continuing
    status = chefclient_login(CHEF_LOGIN_FLOW_TYPE_OAUTH2_DEVICECODE);
    if (status != 0) {
        printf("order: failed to login to chef server: %s\n", strerror(errno));
        goto cleanup;
    }

    // now handle the command that was passed


cleanup:
    chefclient_logout();
    chefclient_cleanup();
    return status;
}
