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
#include <chef/api/account.h>
#include <chef/api/package.h>
#include <chef/api/package_settings.h>
#include <chef/client.h>
#include <chef/platform.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void account_setup(void);

static void __print_help(void)
{
    printf("Usage: order package <command> [options]\n");
    printf("\n");
    printf("Commands:\n");
    printf("  list                        list all packs registered the current account\n");
    printf("  list <pack>                 list all settings for the specific pack\n");
    printf("  set <pack> <param> <value>  sets a specific pack parameter\n");
    printf("  get <pack> <param>          retrieves the value of a specific pack parameter\n");
//    printf("  delete <pack> <*>           deletes either a specific pack, channel, platform, architecture or version\n");
    printf("\n");
    printf("Options:\n");
    printf("  -h, --help\n");
    printf("      Print this help message\n");
}

static const char* __get_publisher_name(void)
{
    struct chef_account* account;
    int                  status;
    const char*          name;

    status = chef_account_get(&account);
    if (status != 0) {
        if (status == -ENOENT) {
            printf("order: no account information available yet\n");
        }
        return NULL;
    }

    name = platform_strdup(chef_account_get_publisher_name(account));
    chef_account_free(account);
    return name;
}

static void __print_packages(struct chef_package** packages, int count)
{
    if (count == 0) {
        printf("no packages found\n");
        return;
    }

    printf("packages:\n");
    for (int i = 0; i < count; i++) {
        printf("  * %s/%s\n", packages[i]->publisher, packages[i]->package);
    }
}

static int __handle_list_packages(void)
{
    struct chef_package**   packages;
    int                     packageCount;
    struct chef_find_params params        = { 0 };
    const char*             publisherName = NULL;
    char*                   query;
    int                     status;

    publisherName = __get_publisher_name();
    if (publisherName == NULL) {
        return -1;
    }

    // allocate memory for the query, which we will write like this
    // publisher/
    query = malloc(strlen(publisherName) + 2);
    if (query == NULL) {
        free((void*)publisherName);
        return -1;
    }

    sprintf(query, "%s/", publisherName);
    free((void*)publisherName);

    // we want all undiscoverable as well
    params.query      = query;
    params.privileged = 1;

    status = chefclient_pack_find(&params, &packages, &packageCount);
    free(query);
    if (status != 0) {
        printf("order: failed to retrieve packages: %s\n", strerror(status));
        return -1;
    }
    
    __print_packages(packages, packageCount);
    if (packages) {
        for (int i = 0; i < packageCount; i++) {
            chef_package_free(packages[i]);
        }
        free(packages);
    }
    return 0;
}

static void __print_settings(struct chef_package_settings* settings)
{
    printf("settings for %s\n", chef_package_settings_get_package(settings));
    printf("  discoverable: %s\n", chef_package_settings_get_discoverable(settings) ? "true" : "false");
}

static int __handle_list(const char* package)
{
    struct chef_package_settings* settings;
    struct chef_settings_params   params = { 0 };
    int                           status;
    const char*                   value;

    if (package == NULL) {
        return __handle_list_packages();
    }
    
    // list variables for a specific package
    params.package = package;

    status = chefclient_pack_settings_get(&params, &settings);
    if (status != 0) {
        if (status == -ENOENT) {
            printf("order: no account information available yet\n");
            account_setup();
            return 0;
        }
        return status;
    }

    __print_settings(settings);
    chef_package_settings_delete(settings);
    return 0;
}

static int __handle_get(const char* package, const char* parameter)
{
    struct chef_package_settings* settings;
    struct chef_settings_params   params = { 0 };
    int                           status;

    if (parameter == NULL) {
        printf("order: no parameter specified for 'package get'\n");
        return -1;
    }

    params.package = package;

    status = chefclient_pack_settings_get(&params, &settings);
    if (status != 0) {
        if (status == -ENOENT) {
            printf("order: no account information available yet\n");
            account_setup();
            return 0;
        }
        return status;
    }

    if (strcmp(parameter, "discoverable") == 0) {
        int discoverable = chef_package_settings_get_discoverable(settings);
        printf("%s: discoverable: %s\n", package, discoverable ? "true" : "false");
    } else {
        printf("order: unknown parameter '%s' for 'package get'\n", parameter);
        return -1;
    }

    chef_package_settings_delete(settings);
    return 0;
}

static int __handle_set(const char* package, const char* parameter, const char* value)
{
    struct chef_package_settings* settings;
    struct chef_settings_params   params = { 0 };
    int                           status;

    if (parameter == NULL) {
        printf("order: no parameter specified for 'package set'\n");
        return -1;
    }

    if (value == NULL) {
        printf("order: no value specified for 'package set'\n");
        return -1;
    }

    params.package = package;

    status = chefclient_pack_settings_get(&params, &settings);
    if (status != 0) {
        if (status == -ENOENT) {
            printf("order: no account information available yet\n");
            account_setup();
            return 0;
        }
        return status;
    }

    if (strcmp(parameter, "discoverable") == 0) {
        chef_package_settings_set_discoverable(settings, strbool(value));
    }
    else {
        printf("order: unknown parameter '%s'\n", parameter);
        return -1;
    }

    status = chefclient_pack_settings_update(settings);
    if (status != 0) {
        fprintf(stderr, "order: failed to update package settings: %s\n", strerror(errno));
        return -1;
    }
    
    chef_package_settings_delete(settings);
    return 0;
}

int package_main(int argc, char** argv)
{
    char* command   = NULL;
    char* package   = NULL;
    char* parameter = NULL;
    char* value     = NULL;
    int   status;

    if (argc > 2) {
        for (int i = 2; i < argc; i++) {
            if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
                __print_help();
                return 0;
            } else {
                if (command == NULL) {
                    command = argv[i];
                } else if (package == NULL) {
                    package = argv[i];
                } else if (parameter == NULL) {
                    parameter = argv[i];
                } else if (value == NULL) {
                    value = argv[i];
                }
            }
        }
    }

    if (command == NULL) {
        printf("order: no command was specified for 'package'\n");
        __print_help();
        return -1;
    }

    // initialize chefclient
    status = chefclient_initialize();
    if (status != 0) {
        fprintf(stderr, "order: failed to initialize chefclient: %s\n", strerror(errno));
        return -1;
    }
    atexit(chefclient_cleanup);

    // do this in a loop, to catch cases where our login token has
    // expired
    while (1) {
        // login before continuing
        status = chefclient_login(CHEF_LOGIN_FLOW_TYPE_OAUTH2_DEVICECODE);
        if (status != 0) {
            printf("order: failed to login to chef server: %s\n", strerror(errno));
            break;
        }

        // now handle the command that was passed
        if (!strcmp(command, "list")) {
            status = __handle_list(package);
        } else if (!strcmp(command, "set")) {
            status = __handle_set(package, parameter, value);
        } else if (!strcmp(command, "get")) {
            status = __handle_get(package, parameter);
        } else {
            printf("order: unknown command '%s'\n", command);
            status = -1;
            break;
        }

        if (status != 0) {
            if (status == -EACCES) {
                chefclient_logout();
                continue;
            }
        }
        break;
    }
    
    return status;
}
