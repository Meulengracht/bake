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
#include <chef/api/package.h>
#include <chef/client.h>
#include <chef/platform.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static void __print_help(void)
{
    printf("Usage: order find <publisher/pack> [options]\n");
    printf("Examples:\n");
    printf("  order find chef     retrieves a list of all packs that contain the word 'chef'\n");
    printf("  order find pub/     retrieves a list of all packs from the publisher 'pub'\n");
    printf("  order find pub/chef retrieves a list of all packs from the publisher 'pub', which also contains the word 'chef'\n");
    printf("Options:\n");
    printf("  -h, --help\n");
    printf("      Print this help message\n");
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

int find_main(int argc, char** argv)
{
    struct chef_find_params params       = { 0 };
    struct chef_package**   packages     = NULL;
    int                     packageCount = 0;
    int                     status;

    if (argc > 2) {
        for (int i = 2; i < argc; i++) {
            if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
                __print_help();
                return 0;
            } else {
                params.query = argv[i];
            }
        }
    }

    if (params.query == NULL) {
        fprintf(stderr, "order: missing search string\n");
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

    status = chefclient_pack_find(&params, &packages, &packageCount);
    if (status != 0) {
        printf("order: failed to find packages related to %s: %s\n", params.query, strerror(errno));
        return -1;
    }

    __print_packages(packages, packageCount);
    if (packages) {
        for (int i = 0; i < packageCount; i++) {
            chef_package_free(packages[i]);
        }
        free(packages);
    }
    return status;
}
