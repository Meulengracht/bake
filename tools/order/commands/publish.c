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
#include <chef/cli.h>
#include <chef/client.h>
#include <chef/platform.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int  account_login_setup(void);
extern void account_publish_setup(void);

static void __print_help(void)
{
    printf("Usage: order publish <pack-path> [options]\n");
    printf("Options:\n");
    printf("  -p, --publisher\n");
    printf("      The publisher that the package should be published under, defaults only if there is one publisher\n");
    printf("  -c, --channel\n");
    printf("      The channel that should be published to, default is devel\n");
    printf("  -h, --help\n");
    printf("      Print this help message\n");
}

static int __ensure_publisher_valid(char** publisher)
{
    struct chef_account* account;
    int                  verified = 0;
    int                  status;

    status = chef_account_get(&account);
    if (status) {
        return status;
    }

    // verify account status is active
    if (chef_account_get_status(account) != CHEF_ACCOUNT_STATUS_ACTIVE) {
        printf("account has been suspended, you are not allowed to publish new packages\n");
        errno = EACCES;
        return -1;
    }

    // if publisher is NULL, then we see if there is just one
    if (*publisher == NULL) {
        if (chef_account_get_publisher_count(account) > 1)  {
            fprintf(stderr, "order: a publisher was not specified and one could not be inferred\n");
            return -1;
        }

        *publisher = platform_strdup(chef_account_get_publisher_name(account, 0));
        if (*publisher == NULL) {
            fprintf(stderr, "order: no publisher for the package\n");
            return -1;
        }
    }

    for (int i = 0; i < chef_account_get_publisher_count(account); i++) {
        if (strcmp(*publisher, chef_account_get_publisher_name(account, i)) == 0) {
            if (chef_account_get_publisher_verified_status(account, i) != CHEF_ACCOUNT_VERIFIED_STATUS_VERIFIED) {
                fprintf(stderr, "order: publisher name has not been verified yet, please wait for verification status to be approved\n");
                errno = EACCES;
                return -1;
            }
            verified = 1;
            break;
        }
    }

    if (verified == 0) {
        fprintf(stderr, "order: publisher name was invalid\n");
        errno = EACCES;
        return -1;
    }

    chef_account_free(account);
    return 0;
}

int publish_main(int argc, char** argv)
{
    struct chef_publish_params params    = { 0 };
    struct chef_package*       package   = NULL;
    struct chef_version*       version   = NULL;
    char*                      packPath  = NULL;
    char*                      publisher = NULL;
    int                        status;

    // set default channel
    params.channel = "devel";

    if (argc > 2) {
        for (int i = 2; i < argc; i++) {
            if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
                __print_help();
                return 0;
            } else if (!__parse_string_switch(argv, argc, &i, "-c", 2, "--channel", 9, NULL, (char**)&params.channel)) {
                continue;
            } else if (!__parse_string_switch(argv, argc, &i, "-p", 2, "--publisher", 11, NULL, &publisher)) {
                continue;
            } else {
                if (packPath != NULL) {
                    printf("only one pack path can be specified\n");
                    return -1;
                }

                packPath = argv[i];
            }
        }
    }

    // parse the pack for all the information we need
    if (packPath == NULL) {
        printf("no pack path specified\n");
        return -1;
    }

    status = chef_package_load(packPath, &package, &version, NULL, NULL);
    if (status != 0) {
        printf("failed to load package: %s\n", strerror(errno));
        return -1;
    }

    // dump information
    printf("publishing package: %s\n", package->package);
    printf("platform:           %s\n", package->platform);
    printf("architecture:       %s\n", package->arch);
    printf("channel:            %s\n", params.channel);
    printf("version:            %d.%d.%d\n", version->major, version->minor, version->patch);

    // set the parameter values
    params.package = package;
    params.version = version;

    // initialize chefclient
    status = chefclient_initialize();
    if (status != 0) {
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

        status = __ensure_publisher_valid(&publisher);
        if (status) {
            if (status == -EACCES) {
                chefclient_logout();
                continue;
            }
            printf("failed to setup neccessary account information: %s\n", strerror(-status));
            break;
        }

        // publish the package
        status = chefclient_pack_publish(&params, packPath);
        if (status != 0) {
            free(publisher);
            printf("failed to publish package: %s\n", strerror(errno));
            break;
        }

        printf("package has been added to the publish queue, it can take up to 10 minuttes "
               "before the package has been published, it depends on the server load and size "
               "of the package. You can check when the package version has changed by running\n"
               "'order info %s/%s'\n", publisher, package->package);
        free(publisher);
        break;
    }
    
    chef_version_free(version);
    chef_package_free(package);
    return status;
}
