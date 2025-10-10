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
#include <chef/api/package.h>
#include <chef/api/package_settings.h>
#include <chef/client.h>
#include <chef/platform.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int  account_login_setup(void);
extern void account_publish_setup(void);

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

static void __print_packages(struct chef_find_result** packages, int count)
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

static int __list_packages_by_publisher(const char* publisherName)
{
    struct chef_find_result** packages;
    int                       packageCount;
    struct chef_find_params   params  = { 0 };
    char*                     query;
    int                       status;

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
    chefclient_pack_find_free(packages, packageCount);
    return 0;
}

static int __handle_list_packages(void)
{
    struct chef_account* account;
    int                  status;

    status = chef_account_get(&account);
    if (status) {
        return status;
    }

    for (int i = 0; i < chef_account_get_publisher_count(account); i++) {
        const char* name = chef_account_get_publisher_name(account, i);
        printf("Packages for %s:\n", name);
        status = __list_packages_by_publisher(name);
        if (status) {
            fprintf(stderr, "order: failed to list packages for %s\n", name);
            break;
        }
    }

    chef_account_free(account);
    return 0;
}

static const char* __discoverable_string(enum chef_package_setting_discoverable discoverable) {
    switch (discoverable) {
        case CHEF_PACKAGE_SETTING_DISCOVERABLE_PRIVATE: return "private";
        case CHEF_PACKAGE_SETTING_DISCOVERABLE_PUBLIC: return "public";
        case CHEF_PACKAGE_SETTING_DISCOVERABLE_COLLABORATORS: return "collaborators";
    }
    return "unknown";
}

static void __print_settings(const char* name, struct chef_package_settings* settings)
{
    enum chef_package_setting_discoverable discoverable;

    discoverable = chef_package_settings_get_discoverable(settings);

    printf("settings for %s\n", name);
    printf("  discoverable: %s\n", __discoverable_string(discoverable));
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
            account_publish_setup();
            return 0;
        }
        return status;
    }

    __print_settings(package, settings);
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
            account_publish_setup();
            return 0;
        }
        return status;
    }

    if (strcmp(parameter, "discoverable") == 0) {
        int discoverable = chef_package_settings_get_discoverable(settings);
        printf("%s: discoverable: %s\n", package, __discoverable_string(discoverable));
    } else {
        printf("order: unknown parameter '%s' for 'package get'\n", parameter);
        return -1;
    }

    chef_package_settings_delete(settings);
    return 0;
}

static enum chef_package_setting_discoverable __discoverable_from_string(const char* value) {
    if (strcmp(value, "private") == 0) {
        return CHEF_PACKAGE_SETTING_DISCOVERABLE_PRIVATE;
    } else if (strcmp(value, "public") == 0) {
        return CHEF_PACKAGE_SETTING_DISCOVERABLE_PUBLIC;
    } else if (strcmp(value, "collaborators") == 0) {
        return CHEF_PACKAGE_SETTING_DISCOVERABLE_COLLABORATORS;
    }
    fprintf(stderr, "order: invalid option value for discoverable: %s", value);
    exit(-1);

    // not reached
    return CHEF_PACKAGE_SETTING_DISCOVERABLE_PRIVATE;
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
            account_publish_setup();
            return 0;
        }
        return status;
    }

    if (strcmp(parameter, "discoverable") == 0) {
        chef_package_settings_set_discoverable(settings, __discoverable_from_string(value));
    } else {
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
        // ensure we are logged in
        if (account_login_setup()) {
            fprintf(stderr, "order: failed to login: %s\n", strerror(errno));
            return -1;
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
