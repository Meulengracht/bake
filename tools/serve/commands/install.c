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
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "chef_served_local_upload_service_client.h"
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
        // Unsigned local installs are still allowed when explicitly requested;
        // this preflight only checks that the package can be loaded.
    } else if (proof[0] == '\0') {
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

static int __upload_local_resource(
    gracht_client_t* client,
    const char*      resourceId,
    const char*      path)
{
    struct gracht_message_context context;
    struct platform_stat          stats;
    char                          sessionId[128] = { 0 };
    unsigned char                 buffer[256 * 1024];
    FILE*                         file = NULL;
    uint64_t                      offset = 0;
    unsigned int                  index = 0;
    int                           status = -1;

    if (resourceId == NULL || resourceId[0] == '\0' || path == NULL) {
        errno = EINVAL;
        return -1;
    }

    status = platform_stat(path, &stats);
    if (status != 0) {
        return status;
    }

    if (stats.size > UINT_MAX) {
        errno = EFBIG;
        return -1;
    }

    file = fopen(path, "rb");
    if (file == NULL) {
        return -1;
    }

    status = chef_served_local_upload_open(client, &context, resourceId, (unsigned int)stats.size);
    if (status != 0) {
        goto cleanup;
    }

    status = gracht_client_wait_message(client, &context, GRACHT_MESSAGE_BLOCK);
    if (status != 0) {
        goto cleanup;
    }

    status = chef_served_local_upload_open_result(client, &context, &sessionId[0], sizeof(sessionId));
    if (status != 0 || sessionId[0] == '\0') {
        status = -1;
        goto cleanup;
    }

    while (offset < stats.size) {
        size_t bytesRead = fread(&buffer[0], 1, sizeof(buffer), file);
        if (bytesRead == 0) {
            status = ferror(file) ? -1 : 0;
            break;
        }

        status = chef_served_local_upload_write_chunk(client, NULL, &sessionId[0], index, &buffer[0], (uint32_t)bytesRead);
        if (status != 0) {
            goto cleanup;
        }

        offset += bytesRead;
        index++;
    }

    if (status != 0) {
        goto cleanup;
    }

    status = chef_served_local_upload_finish(client, NULL, &sessionId[0]);
    if (status != 0) {
        goto cleanup;
    }

    status = chef_served_local_upload_close(client, NULL, &sessionId[0]);

cleanup:
    if (file != NULL) {
        fclose(file);
    }
    return status;
}

static int __install_local_package(
    gracht_client_t* client,
    const char*      packagePath,
    const char*      proofPath,
    int              allowUnsigned,
    unsigned int*    transactionIdOut)
{
    struct gracht_message_context        context;
    struct chef_served_install_local_options options = { 0 };
    struct chef_served_install_local_session session = { 0 };
    struct platform_stat                 packageStats;
    struct platform_stat                 proofStats;
    int                                  status;

    if (transactionIdOut != NULL) {
        *transactionIdOut = 0;
    }

    status = platform_stat(packagePath, &packageStats);
    if (status != 0) {
        return status;
    }

    options.allow_unsigned = allowUnsigned;
    options.package_size = (size_t)packageStats.size;
    if (proofPath != NULL && proofPath[0] != '\0') {
        status = platform_stat(proofPath, &proofStats);
        if (status != 0) {
            return status;
        }
        options.proof_size = (size_t)proofStats.size;
    }

    status = chef_served_install_local_begin(client, &context, &options);
    if (status != 0) {
        return status;
    }

    status = gracht_client_wait_message(client, &context, GRACHT_MESSAGE_BLOCK);
    if (status != 0) {
        return status;
    }

    status = chef_served_install_local_begin_result(client, &context, &session);
    if (status != 0) {
        return status;
    }

    if (session.import_id == NULL || session.package_resource_id == NULL) {
        chef_served_install_local_session_destroy(&session);
        errno = EPROTO;
        return -1;
    }

    status = __upload_local_resource(client, session.package_resource_id, packagePath);
    if (status != 0) {
        chef_served_install_local_cancel(client, NULL, session.import_id);
        chef_served_install_local_session_destroy(&session);
        return status;
    }

    if (proofPath != NULL && proofPath[0] != '\0') {
        if (session.proof_resource_id == NULL || session.proof_resource_id[0] == '\0') {
            chef_served_install_local_cancel(client, NULL, session.import_id);
            chef_served_install_local_session_destroy(&session);
            errno = EPROTO;
            return -1;
        }

        status = __upload_local_resource(client, session.proof_resource_id, proofPath);
        if (status != 0) {
            chef_served_install_local_cancel(client, NULL, session.import_id);
            chef_served_install_local_session_destroy(&session);
            return status;
        }
    }

    status = chef_served_install_local_end(client, &context, session.import_id);
    if (status == 0) {
        status = gracht_client_wait_message(client, &context, GRACHT_MESSAGE_BLOCK);
    }
    if (status == 0 && transactionIdOut != NULL) {
        status = chef_served_install_local_end_result(client, &context, transactionIdOut);
    }

    chef_served_install_local_session_destroy(&session);
    return status;
}

static int __install_from_local(
    gracht_client_t* client,
    const char*      package_path,
    const char*      proof_path,
    int              allow_unsigned)
{
    unsigned int transactionId = 0;
    int          status;

    status = __verify_package(package_path, proof_path);
    if (status) {
        return status;
    }
    
    status = __install_local_package(
        client,
        package_path,
        proof_path,
        allow_unsigned, 
        &transactionId
    );
    if (status) {
        printf("communication error: %i\n", status);
    }
    return status;  
}

static int __install_from_store(gracht_client_t* client, struct chef_served_install_options* options)
{
    struct gracht_message_context context;
    unsigned int                  transactionId = 0;
    int                           status;

    status = chef_served_install(client, &context, options);
    if (status) {
        printf("communication error: %i\n", status);
        return status;
    }

    status = gracht_client_wait_message(client, &context, GRACHT_MESSAGE_BLOCK);
    if (status) {
        printf("communication error: %i\n", status);
        return status;
    }

    status = chef_served_install_result(client, &context, &transactionId);
    if (status) {
        printf("communication error: %i\n", status);
        return status;
    }
    return 0;
}

struct __install_options {
    struct chef_served_install_options chef_options;
    int                                allow_unsigned;
    const char*                        package_path;
    const char*                        proof_path;
};

static int __is_package_file(const char* package)
{
    struct platform_stat stats;
    return platform_stat(package, &stats) == 0 && stats.type == PLATFORM_FILETYPE_FILE;
}

static int __parse_options(struct __install_options* options, int argc, char** argv)
{
    const char* package = NULL;
    int         revision = 0;

    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            __print_help();
            return -1;
        } else if (!__parse_string_switch(argv, argc, &i, "-C", 2, "--channel", 9, NULL, (char**)&options->chef_options.channel)) {
            continue;
        } else if (!__parse_string_switch(argv, argc, &i, "-P", 2, "--proof", 7, NULL, (char**)&options->proof_path)) {
            continue;
        } else if (!__parse_quantity_switch(argv, argc, &i, "-R", 2, "--revision", 10, 0, &revision)) {
            options->chef_options.revision = (int)revision;
            continue;
        } else if (!strcmp(argv[i], "--allow-unsigned")) {
            options->allow_unsigned = 1;
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

    // if the package looks like a file, treat it as a local install
    if (package != NULL && __is_package_file(package)) {
        options->package_path = package;

        // In this case, the proof path *must* be provided
        if (options->proof_path == NULL) {
            char* defaultProofPath = __default_local_proof_path(package);
            if (defaultProofPath != NULL && __proof_file_exists(defaultProofPath)) {
                options->proof_path = defaultProofPath;
            }
            
            if (options->proof_path == NULL && !options->allow_unsigned) {
                fprintf(stderr, "Missing local proof. Provide --proof or use --allow-unsigned in development mode.\n");
                free(defaultProofPath);
                return -1;
            }
        }
    } else {
        options->chef_options.package = package;
    }

    return 0;
}

int install_main(int argc, char** argv)
{
    struct __install_options installOptions = { 0 };
    gracht_client_t*         client;
    int                      status;

    // set default channel
    installOptions.chef_options.channel = "stable";

    if (argc > 2) {
        status = __parse_options(&installOptions, argc, argv);
        if (status) {
            return status;
        }
    }

    if (installOptions.package_path == NULL && installOptions.chef_options.package == NULL) {
        printf("no package specified\n");
        __print_help();
        return -1;
    }
    
    status = __chef_client_initialize(&client);
    if (status != 0) {
        printf("failed to initialize client: %s\n", strerror(status));
        return status;
    }

    // is the package a path? otherwise try to download from
    // official repo
    if (installOptions.package_path != NULL) {
        status = __install_from_local(client, installOptions.package_path, installOptions.proof_path, installOptions.allow_unsigned);
    } else {
        status = __install_from_store(client, &installOptions.chef_options);
    }
    
cleanup:
    gracht_client_shutdown(client);
    return status;
}
