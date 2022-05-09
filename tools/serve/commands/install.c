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

#include <chef/api/package.h>
#include <chef/client.h>
#include <errno.h>
#include <gracht/client.h>
#include <chef/platform.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "chef_served_service_client.h"

#define __TEMPORARY_FILENAME ".cheftmpdl"

extern int __chef_client_initialize(gracht_client_t** clientOut);

static const char* g_installMsgs[] = {
    "success",
    "verification failed, invalid or corrupt package",
    "package installation failed due to technical problems", // lol lets improve this some day, but view chef logs for details
    "package was installed but failed to load applications",
    "package was installed but failed to execute hooks, package is in undefined state"
};

static void __print_help(void)
{
    printf("Usage: serve install <pack> [options]\n");
    printf("Options:\n");
    printf("  -c, --channel\n");
    printf("      Install from a specific channel, default: stable\n");
    printf("  -h, --help\n");
    printf("      Print this help message\n");
}

static int __parse_package_identifier(const char* id, const char** publisherOut, const char** nameOut)
{
    char** names;
    int    count = 0;

    // split the publisher/package
    names = strsplit(id, '/');
    if (names == NULL) {
        fprintf(stderr, "unknown package name or path: %s\n", id);
        return -1;
    }
    
    while (names[count] != NULL) {
        count++;
    }

    if (count != 2) {
        fprintf(stderr, "unknown package name or path: %s\n", id);
        return -1;
    }

    *publisherOut = strdup(names[0]);
    *nameOut      = strdup(names[1]);
    strsplit_free(names);
    return 0;
}

static void __cleanup(void)
{
    platform_unlink(__TEMPORARY_FILENAME);
}

int install_main(int argc, char** argv)
{
    gracht_client_t*            client;
    int                         status;
    struct platform_stat        stats;
    struct chef_download_params params  = { 0 };
    const char*                 package = NULL;
    char*                       fullpath;

    // set default channel
    params.channel = "stable";

    if (argc > 2) {
        for (int i = 2; i < argc; i++) {
            if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
                __print_help();
                return 0;
            } else if (!strcmp(argv[i], "-c") || !strcmp(argv[i], "--channel")) { 
                char* channel = strchr(argv[i], '=');
                if (channel) {
                    channel++;
                    params.channel = channel;
                } else {
                    printf("missing recipe name for --channel=...\n");
                    return -1;
                }
            } else if (argv[i][0] != '-') {
                if (package == NULL) {
                    package = argv[i];
                } else {
                    printf("unknown option: %s\n", argv[i]);
                    __print_help();
                    return -1;
                }
            }
        }
    }

    if (package == NULL) {
        printf("no package specified for install\n");
        __print_help();
        return -1;
    }

    status = chefclient_initialize();
    if (status != 0) {
        fprintf(stderr, "failed to initialize chef client\n");
        return -1;
    }
    atexit(chefclient_cleanup);

    // is the package a path? otherwise try to download from
    // official repo
    if (platform_stat(package, &stats)) {
        status = __parse_package_identifier(package, &params.publisher, &params.package);
        if (status != 0) {
            return status;
        }

        // we only allow installs from native packages
        params.platform = CHEF_PLATFORM_STR;
        params.arch     = CHEF_ARCHITECTURE_STR;

        printf("downloading package %s...\n", package);
        status = chefclient_pack_download(&params, __TEMPORARY_FILENAME);
        if (status != 0) {
            printf("failed to download package: %s\n", strerror(status));
            return status;
        }

        package = __TEMPORARY_FILENAME;
    }

    // at this point package points to a file in our PATH
    // but we need the absolute path
    fullpath = platform_abspath(package);
    if (fullpath == NULL) {
        printf("failed to get resolve package path: %s\n", package);
        return -1;
    }

    status = __chef_client_initialize(&client);
    if (status != 0) {
        printf("failed to initialize client: %s\n", strerror(status));
        return status;
    }

    printf("installing package: %s... ", package);
    status = chef_served_install(client, NULL, fullpath);
    if (status != 0) {
        printf("communication error: %i\n", status);
        goto cleanup;
    }

    gracht_client_wait_message(client, NULL, GRACHT_MESSAGE_BLOCK);

cleanup:
    gracht_client_shutdown(client);
    free(fullpath);
    return status;
}

void chef_served_event_package_installed_invocation(gracht_client_t* client, const enum chef_install_status status, const struct chef_served_package* info)
{
    printf("installation status: %s\n", g_installMsgs[status]);
    if (status == CHEF_INSTALL_STATUS_SUCCESS) {
        // print package info
    }
}
