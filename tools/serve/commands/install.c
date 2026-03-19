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
#include <chef/cli.h>
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
    printf("  -C, --channel\n");
    printf("      Install from a specific channel, default: stable\n");
    printf("  -R, --revision\n");
    printf("      Install a specific revision of the package\n");
    printf("  -P, --proof\n");
    printf("      If the package is a local file, use this proof instead of the default <pack>.proof\n");
    printf("  --allow-unsigned\n");
    printf("      Allow installing a local package without proof. Intended for development only.\n");
    printf("  -h, --help\n");
    printf("      Print this help message\n");
}

static char* __default_local_proof_path(const char* packagePath)
{
    char*  proofPath;
    size_t length;

    length = strlen(packagePath);
    proofPath = malloc(length + strlen(".proof") + 1);
    if (proofPath == NULL) {
        return NULL;
    }

    sprintf(proofPath, "%s.proof", packagePath);
    return proofPath;
}

static int __proof_file_exists(const char* proofPath)
{
    struct platform_stat stats;
    return platform_stat(proofPath, &stats) == 0;
}

static int __verify_package(const char* path, const char* proof)
{
    struct chef_package* package;
    int                  status;

    if (proof == NULL) {
        fprintf(stderr, "no proof was provided for the local package\n");
        return -1;
    }

    // dont care about commands
    status = chef_package_load(path, &package, NULL, NULL, NULL);
    if (status != 0) {
        fprintf(stderr, "failed to load package: %s\n", path);
        return -1;
    }

    // free resources
    chef_package_free(package);
    return 0;
}

int install_main(int argc, char** argv)
{
    struct chef_served_install_options installOptions = { 0 };
    gracht_client_t*                   client;
    int                                status;
    struct platform_stat               stats;
    uint64_t                           revision;
    const char*                        package   = NULL;
    char*                              fullpath  = NULL;
    char*                              defaultProofPath = NULL;

    // set default channel
    installOptions.channel = "stable";

    if (argc > 2) {
        for (int i = 2; i < argc; i++) {
            if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
                __print_help();
                return 0;
            } else if (!__parse_string_switch(argv, argc, &i, "-C", 2, "--channel", 9, NULL, (char**)&installOptions.channel)) {
                continue;
            } else if (!__parse_string_switch(argv, argc, &i, "-P", 2, "--proof", 7, NULL, &installOptions.proof)) {
                continue;
            } else if (!__parse_quantity_switch(argv, argc, &i, "-R", 2, "--revision", 10, 0, &revision)) {
                installOptions.revision = (int)revision;
                continue;
            } else if (!strcmp(argv[i], "--allow-unsigned")) {
                installOptions.allow_unsigned = 1;
                continue;
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
        printf("no package specified\n");
        __print_help();
        return -1;
    }

    // is the package a path? otherwise try to download from
    // official repo
    if (platform_stat(package, &stats) == 0) {
        if (installOptions.proof == NULL) {
            defaultProofPath = __default_local_proof_path(package);
            if (defaultProofPath != NULL && __proof_file_exists(defaultProofPath)) {
                installOptions.proof = defaultProofPath;
            }
        }

        if (installOptions.proof == NULL && !installOptions.allow_unsigned) {
            fprintf(stderr, "Missing local proof. Provide --proof or use --allow-unsigned in development mode.\n");
            free(defaultProofPath);
            return -1;
        }

        status = __verify_package(package, installOptions.proof);
        if (status != 0) {
            free(defaultProofPath);
            return status;
        }
        
        fullpath = platform_abspath(package);
        if (fullpath == NULL) {
            printf("failed to get resolve package path: %s\n", package);
            return -1;
        }

        installOptions.path = fullpath;
    } else {
        installOptions.package = (char*)package;
    }

    // at this point package points to a file in our PATH
    // but we need the absolute path
    status = __chef_client_initialize(&client);
    if (status != 0) {
        printf("failed to initialize client: %s\n", strerror(status));
        return status;
    }

    status = chef_served_install(client, NULL, &installOptions);
    if (status != 0) {
        printf("communication error: %i\n", status);
        goto cleanup;
    }

    gracht_client_wait_message(client, NULL, GRACHT_MESSAGE_BLOCK);

cleanup:
    gracht_client_shutdown(client);
    free(defaultProofPath);
    free(fullpath);
    return status;
}
