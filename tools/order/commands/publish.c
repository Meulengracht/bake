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
    printf("Usage: order publish <pack-path> [options]\n");
    printf("Options:\n");
    printf("  -c, --channel\n");
    printf("      The channel that should be published to, default is devel\n");
    printf("  -h, --help\n");
    printf("      Print this help message\n");
}

int publish_main(int argc, char** argv)
{
    struct chef_publish_params params   = { 0 };
    struct chef_package*       package  = NULL;
    struct chef_version*       version  = NULL;
    char*                      packPath = NULL;
    int                        status;

    // set default channel
    params.channel = "devel";

    if (argc > 2) {
        for (int i = 2; i < argc; i++) {
            if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
                __print_help();
                return 0;
            }
            else if (!strcmp(argv[i], "-c") || !strcmp(argv[i], "--channel")) {
                params.channel = argv[++i];
            }
            else {
                if (packPath != NULL) {
                    printf("order: only one pack path can be specified\n");
                    return -1;
                }

                packPath = argv[i];
            }
        }
    }

    // parse the pack for all the information we need
    if (packPath == NULL) {
        printf("order: no pack path specified\n");
        return -1;
    }

    status = chef_package_load(packPath, &package, &version);
    if (status != 0) {
        printf("order: failed to load package: %s\n", strerror(errno));
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

    // publish the package
    status = chefclient_pack_publish(&params, packPath);
    if (status != 0) {
        printf("order: failed to publish package: %s\n", strerror(errno));
        goto cleanup;
    }

    printf("order: package published successfully\n");

cleanup:
    chef_version_free(version);
    chef_package_free(package);
    chefclient_logout();
    chefclient_cleanup();
    return status;
}
