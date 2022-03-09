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
#include <chef/account.h>
#include <chef/client.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void account_setup(void);

static void __print_help(void)
{
    printf("Usage: order account <command> [options]\n");
    printf("\n");
    printf("Commands:\n");
    printf("  whoami              shows information about the currently logged in user\n");
    printf("  set <param> <value> sets a specific account parameter\n");
    printf("  get <param>         retrieves the value of a specific account paramater\n");
    printf("\n");
    printf("Options:\n");
    printf("  -h, --help\n");
    printf("      Print this help message\n");
}

static int __handle_whoami(void)
{
    struct chef_account* account;
    int                  status;

    status = chef_account_get(&account);
    if (status != 0) {
        if (errno == ENOENT) {
            printf("order: no account information available yet\n");
            account_setup();
            return 0;
        }
        return -1;
    }

    printf("order: account information\n");
    printf("  publisher name: %s\n", chef_account_get_publisher_name(account));

    chef_account_free(account);
    return 0;
}

static int __handle_get(char* parameter)
{
    struct chef_account* account;
    int                  status;
    const char*          value;

    if (parameter == NULL) {
        printf("order: no parameter specified for 'account get'\n");
        return -1;
    }

    status = chef_account_get(&account);
    if (status != 0) {
        if (errno == ENOENT) {
            printf("order: no account information available yet\n");
            account_setup();
            return 0;
        }
        return -1;
    }

    if (strcmp(parameter, "publisher-name") == 0) {
        value = chef_account_get_publisher_name(account);
    }
    else {
        printf("order: unknown parameter '%s' for 'account get'\n", parameter);
        return -1;
    }

    printf("order: %s: %s\n", parameter, value);
    chef_account_free(account);
    return 0;
}

static int __handle_set(char* parameter, char* value)
{
    struct chef_account* account;
    int                  status;

    if (parameter == NULL) {
        printf("order: no parameter specified for 'account set'\n");
        return -1;
    }

    if (value == NULL) {
        printf("order: no value specified for 'account set'\n");
        return -1;
    }

    status = chef_account_get(&account);
    if (status != 0) {
        if (errno == ENOENT) {
            printf("order: no account information available yet\n");
            account_setup();
            return 0;
        }
        return -1;
    }

    if (strcmp(parameter, "publisher-name") == 0) {
        chef_account_set_publisher_name(account, value);
    }
    else {
        printf("order: unknown parameter '%s'\n", parameter);
        return -1;
    }

    status = chef_account_update(account);
    if (status != 0) {
        fprintf(stderr, "order: failed to update account information: %s\n", strerror(errno));
        return -1;
    }
    
    chef_account_free(account);
    return 0;
}

int account_main(int argc, char** argv)
{
    char* command   = NULL;
    char* parameter = NULL;
    char* value     = NULL;
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
                else if (parameter != NULL) {
                    parameter = argv[i];
                }
                else if (value != NULL) {
                    value = argv[i];
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
    if (!strcmp(command, "whoami")) {
        status = __handle_whoami();
    }
    else if (!strcmp(command, "set")) {
        status = __handle_set(parameter, value);
    }
    else if (!strcmp(command, "get")) {
        status = __handle_get(parameter);
    }
    else {
        printf("order: unknown command '%s'\n", command);
        status = -1;
    }

cleanup:
    chefclient_logout();
    chefclient_cleanup();
    return status;
}
