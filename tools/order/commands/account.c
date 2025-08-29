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
#include <chef/api/account.h>
#include <chef/client.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int  account_login_setup(void);
extern void account_publish_setup(void);

static void __print_help(void)
{
    printf("Usage: order account <command> [options]\n");
    printf("\n");
    printf("Commands:\n");
    printf("  whoami              shows information about the currently logged in user\n");
    printf("\n  api-key           allows management of api-keys for the current account\n");
    printf("  api-key create <name>  creates a new api-key with the specified name\n");
    printf("  api-key delete <name>  deletes the api-key with the specified id\n");
    printf("  api-key list           lists all api-keys for the current account\n");
    printf("\n  publisher         allows management of publishers for the current account\n");
    printf("  publisher register <name>              registers a new publisher with the specified name\n");
    printf("  publisher get <name> <option>          retrieves information about a specific publisher\n");
    printf("  publisher set <name> <option> <value>  sets the configuration option");
    printf("\n  set <param> <value> sets a specific account parameter\n");
    printf("  get <param>         retrieves the value of a specific account parameter\n");
    printf("  logout              logout of the current account\n");
    printf("\n");
    printf("Options:\n");
    printf("  -h, --help\n");
    printf("      Print this help message\n");
}

static const char* __get_verified_status(struct chef_account* account, int index)
{
    enum chef_account_verified_status status = 
        chef_account_get_publisher_verified_status(account, index);
    switch (status) {
        case CHEF_ACCOUNT_VERIFIED_STATUS_PENDING:
            return "name change pending";
        case CHEF_ACCOUNT_VERIFIED_STATUS_VERIFIED:
            return "verified";
        case CHEF_ACCOUNT_VERIFIED_STATUS_REJECTED:
            return "rejected";
        default:
            return "unknown";
    }
}

static const char* __get_status(struct chef_account* account)
{
    enum chef_account_status status = chef_account_get_status(account);
    switch (status) {
        case CHEF_ACCOUNT_STATUS_ACTIVE:
            return "active";
        case CHEF_ACCOUNT_STATUS_LOCKED:
            return "locked";
        case CHEF_ACCOUNT_STATUS_DELETED:
            return "deleted";
        default:
            return "unknown";
    }
}

static int __handle_whoami(void)
{
    struct chef_account* account;
    int                  status;
    int                  publisherCount;

    status = chef_account_get(&account);
    if (status) {
        return status;
    }

    printf("account information\n");
    printf("  name:   %s\n", chef_account_get_name(account));
    printf("  email:  %s\n", chef_account_get_email(account));
    printf("  status: %s\n", __get_status(account));

    publisherCount = chef_account_get_publisher_count(account);
    if (publisherCount == 0) {
        printf("  ---- no publishers registered\n");
        chef_account_free(account);
        return 0;
    }

    printf("\npublishers\n");
    for (int i = 0; i < publisherCount; i++) {
        printf("  publisher %i: %s (%s)\n", i + 1,
            chef_account_get_publisher_name(account, i),
            __get_verified_status(account, i)
        );
    }

    chef_account_free(account);
    return 0;
}

static int __handle_get(char* parameter)
{
    struct chef_account* account;
    int                  status;
    const char*          value;

    if (parameter == NULL) {
        printf("no parameter specified for 'account get'\n");
        return -1;
    }

    status = chef_account_get(&account);
    if (status != 0) {
        return status;
    }

    if (strcmp(parameter, "name") == 0) {
        value = chef_account_get_name(account);
    } else if (strcmp(parameter, "email") == 0) {
        value = chef_account_get_email(account);
    } else {
        printf("unknown parameter '%s' for 'account get'\n", parameter);
        return -1;
    }

    printf("%s: %s\n", parameter, value);
    chef_account_free(account);
    return 0;
}

static int __handle_set(char* parameter, char* value)
{
    struct chef_account* account;
    int                  status;

    if (parameter == NULL) {
        printf("no parameter specified for 'account set'\n");
        return -1;
    }

    if (value == NULL) {
        printf("no value specified for 'account set'\n");
        return -1;
    }

    status = chef_account_get(&account);
    if (status) {
        return status;
    }

    if (strcmp(parameter, "name") == 0) {
        chef_account_set_name(account, value);
    } else if (strcmp(parameter, "email") == 0) {
        chef_account_set_email(account, value);
    } else {
        printf("order: unknown parameter '%s'\n", parameter);
        return -1;
    }

    status = chef_account_update(account);
    if (status) {
        fprintf(stderr, "failed to update account information: %s\n", strerror(errno));
        return status;
    }

    chef_account_free(account);
    return 0;
}

static int __handle_api_key(const char* option, const char* name)
{
    struct chef_account* account;
    int                  status;

    if (option == NULL) {
        printf("no option specified for 'account api-key'\n");
        return -1;
    }

    status = chef_account_get(&account);
    if (status) {
        return status;
    }

    if (strcmp(option, "create") == 0) {
        char* apiKey = NULL;
        if (name == NULL) {
            fprintf(stderr, "order: <name> must be provided for the create option\n");
            chef_account_free(account);
            return status;
        }

        status = chef_account_apikey_create(name, &apiKey);
        if (status == 0) {
            printf("Key %s was created: %s\n", name, apiKey);
            printf("Make sure to backup this key, as this key cannot be shown again\n");
        } else {
            fprintf(stderr, "order: failed to create api-key %s\n", name);
        }
    } else if (strcmp(option, "delete") == 0) {
        if (name == NULL) {
            fprintf(stderr, "order: <name> must be provided for the delete option\n");
            chef_account_free(account);
            return status;
        }

        status = chef_account_apikey_delete(name);
        if (status == 0) {
            printf("Key %s was deleted, any clients using this have been revoked\n", name);
        } else {
            fprintf(stderr, "order: failed to delete api-key %s\n", name);
        }
    } else if (strcmp(option, "list") == 0) {
        printf("\napi-keys\n");
        for (int i = 0; i < chef_account_get_apikey_count(account); i++) {
            printf("  %i: %s\n", i + 1,
                chef_account_get_apikey_name(account, i)
            );
        }
    } else {
        printf("unknown option '%s' for 'account api-key'\n", option);
        status = -1;
    }
    chef_account_free(account);
    return status;
}

static int __handle_publisher_option(const char* option)
{
    struct chef_account* account;
    int                  status;

    if (option == NULL) {
        printf("no option specified for 'account publisher'\n");
        return -1;
    }

    status = chef_account_get(&account);
    if (status) {
        return status;
    }

    if (strcmp(option, "register") == 0) {
        account_publish_setup();
    } else {
        printf("unknown option '%s' for 'account publisher'\n", option);
        return -1;
    }
    return 0;
}

int account_main(int argc, char** argv)
{
    char* command = NULL;
    char* option  = NULL;
    char* value   = NULL;
    int   status;

    if (argc > 2) {
        for (int i = 2; i < argc; i++) {
            if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
                __print_help();
                return 0;
            }
            else {
                if (command == NULL) {
                    command = argv[i];
                } else if (option == NULL) {
                    option = argv[i];
                } else if (value == NULL) {
                    value = argv[i];
                }
            }
        }
    }

    if (command == NULL) {
        printf("no command was specified for 'account'\n");
        __print_help();
        return -1;
    }

    status = chefclient_initialize();
    if (status) {
        fprintf(stderr, "failed to initialize chefclient: %s\n", strerror(errno));
        return -1;
    }
    atexit(chefclient_cleanup);

    // do this in a loop, to catch cases where our login token has
    // expired
    while (1) {
        // ensure we are logged in
        if (account_login_setup()) {
            fprintf(stderr, "order: failed to login: %s\n", strerror(errno));
            return -1;
        }

        // now handle the command that was passed
        if (!strcmp(command, "whoami")) {
            status = __handle_whoami();
        } else if (!strcmp(command, "api-key")) {
            status = __handle_api_key(option, value);
        } else if (!strcmp(command, "publisher")) {
            status = __handle_publisher_option(option);
        } else if (!strcmp(command, "set")) {
            status = __handle_set(option, value);
        } else if (!strcmp(command, "get")) {
            status = __handle_get(option);
        } else if (!strcmp(command, "logout")) {
            status = 0;
            chefclient_logout();
        } else {
            printf("unknown command '%s'\n", command);
            status = -1;
            break;
        }

        if (status) {
            if (status == -EACCES) {
                chefclient_logout();
                continue;
            }
        }
        break;
    }
    
    return status;
}
