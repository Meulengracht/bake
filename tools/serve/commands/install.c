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
#include <chef/package.h>
#include <chef/platform.h>
#include <gracht/client.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "chef_served_service_client.h"

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

static int __ask_yes_no_question(const char* question)
{
    char answer[3];
    printf("%s [y/n] ", question);
    if (fgets(answer, sizeof(answer), stdin) == NULL) {
        return 0;
    }
    return answer[0] == 'y' || answer[0] == 'Y';
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

    *publisherOut = platform_strdup(names[0]);
    *nameOut      = platform_strdup(names[1]);
    strsplit_free(names);
    return 0;
}

static char* __get_unsafe_infoname(struct chef_package* package, struct chef_version* version)
{
    char* name;

    name = malloc(128);
    if (name == NULL) {
        return NULL;
    }

    sprintf(name, "[devel] %s %i.%i.%i",
        package->package, version->major,
        version->minor, version->patch
    );
    return name;
}

static char* __get_safe_infoname(const char* publisher, struct chef_package* package, struct chef_version* version)
{
    char* name;

    name = malloc(128);
    if (name == NULL) {
        return NULL;
    }

    sprintf(name, "%s/%s (verified, revision %i)",
        publisher, package->package, version->revision
    );
    return name;
}

static int __verify_package(const char* path, char** infoNameOut, char** publisherOut)
{
    struct chef_package* package;
    struct chef_version* version;
    int                  status;

    // dont care about commands
    status = chef_package_load(path, &package, &version, NULL, NULL);
    if (status != 0) {
        fprintf(stderr, "failed to load package: %s\n", path);
        return -1;
    }

    // verify revision being non-zero, otherwise we need to warn about
    // the package being a development package
    if (version->revision == 0) {
        fprintf(stderr, "warning: package is a development package, which means chef cannot "
                        "verify its integrity.\n");
        status = __ask_yes_no_question("continue?");
        if (!status) {
            fprintf(stderr, "aborting\n");
            chef_package_free(package);
            chef_version_free(version);
            return -1;
        }

        *infoNameOut  = __get_unsafe_infoname(package, version);
        *publisherOut = platform_strdup("unverified");
    } else {
        *infoNameOut  = __get_safe_infoname(*publisherOut, package, version);
    }

    // free resources
    chef_package_free(package);
    chef_version_free(version);
    return 0;
}

int install_main(int argc, char** argv)
{
    struct chef_served_install_options installOptions = { 0 };
    gracht_client_t*                   client;
    int                                status;
    struct platform_stat               stats;
    const char*                        package   = NULL;
    char*                              fullpath  = NULL;

    // set default channel
    installOptions.channel = "stable";

    if (argc > 2) {
        for (int i = 2; i < argc; i++) {
            if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
                __print_help();
                return 0;
            } else if (!strncmp(argv[i], "-c", 2) || !strncmp(argv[i], "--channel", 9)) { 
                char* channel = strchr(argv[i], '=');
                if (channel) {
                    channel++;
                    installOptions.channel = channel;
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

    // is the package a path? otherwise try to download from
    // official repo
    if (platform_stat(package, &stats) == 0) {
        status = __verify_package(package, &infoName, &publisher);
        if (status != 0) {
            return status;
        }
        
        fullpath = platform_abspath(package);
        if (fullpath == NULL) {
            printf("failed to get resolve package path: %s\n", package);
            return -1;
        }

        installOptions.path = fullpath;
    } else {
        installOptions.package = package;
    }

    // at this point package points to a file in our PATH
    // but we need the absolute path
    status = __chef_client_initialize(&client);
    if (status != 0) {
        printf("failed to initialize client: %s\n", strerror(status));
        return status;
    }

    printf("installing %s\n", infoName);
    status = chef_served_install(client, NULL, &installOptions);
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
