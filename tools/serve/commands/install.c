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
#include <chef/package_manifest.h>
#include <chef/platform.h>
#include <gracht/client.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "chef_served_local_upload_service_client.h"
#include "chef_served_service_client.h"

extern int __chef_client_initialize(gracht_client_t** clientOut);
extern int __chef_client_subscribe(gracht_client_t* client);
extern int __chef_client_await_transaction(gracht_client_t* client, unsigned int transactionId, enum chef_transaction_result* resultOut);

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
    printf("  -d, --detach\n");
    printf("      Submit the install and exit without waiting for completion\n");
    printf("  -h, --help\n");
    printf("      Print this help message\n");
}

static char* __default_local_proof_path(const char* packagePath)
{
    char*  proofPath;
    size_t length;

    length = strlen(packagePath);
    proofPath = malloc(length + 6 + 1);
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
    struct chef_package_manifest* manifest = NULL;
    int                           status;

    if (proof == NULL) {
        // Unsigned local installs are still allowed when explicitly requested;
        // this preflight only checks that the package can be loaded.
    } else if (proof[0] == '\0') {
        fprintf(stderr, "no proof was provided for the local package\n");
        return -1;
    }

    // dont care about commands
    status = chef_package_manifest_load(path, &manifest);
    if (status != 0) {
        fprintf(stderr, "failed to load package: %s\n", path);
        return -1;
    }

    chef_package_manifest_free(manifest);
    return 0;
}

static int __local_session_upload(
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

    status = platform_stat(path, &stats);
    if (status) {
        return status;
    }

    // Maximum upload size is 4GB for now
    if (stats.size > UINT32_MAX) {
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
        if (status) {
            goto cleanup;
        }

        offset += bytesRead;
        index++;
    }

    if (status) {
        goto cleanup;
    }

    status = chef_served_local_upload_finish(client, NULL, &sessionId[0]);
    if (status) {
        goto cleanup;
    }

    status = chef_served_local_upload_close(client, NULL, &sessionId[0]);

cleanup:
    if (file != NULL) {
        fclose(file);
    }
    return status;
}

static int __local_session_start(
    gracht_client_t*                          client,
    const char*                               packagePath,
    const char*                               proofPath,
    struct chef_served_install_local_session* session)
{
    struct gracht_message_context            context;
    struct chef_served_install_local_options options = { 0 };
    struct platform_stat                     stats;
    int                                      status;

    status = platform_stat(packagePath, &stats);
    if (status) {
        return status;
    }
    options.package_size = (size_t)stats.size;
    
    status = platform_stat(proofPath, &stats);
    if (status) {
        return status;
    }
    options.proof_size = (size_t)stats.size;

    status = chef_served_install_local_begin(client, &context, &options);
    if (status) {
        return status;
    }

    status = gracht_client_wait_message(client, &context, GRACHT_MESSAGE_BLOCK);
    if (status) {
        return status;
    }

    status = chef_served_install_local_begin_result(client, &context, session);
    if (status) {
        return status;
    }
    return 0;
}

static int  __local_session_commit(
    gracht_client_t* client,
    const char*      importId,
    unsigned int*    transactionIdOut)
{
    struct gracht_message_context context;
    int                           status;

    status = chef_served_install_local_end(client, &context, importId);
    if (status) {
        return status;
    }

    status = gracht_client_wait_message(client, &context, GRACHT_MESSAGE_BLOCK);
    if (status) {
        return status;
    }

    status = chef_served_install_local_end_result(client, &context, transactionIdOut);
    if (status) {
        return status;
    }
    return 0;
}

static int __install_local_package(
    gracht_client_t* client,
    const char*      packagePath,
    const char*      proofPath,
    unsigned int*    transactionIdOut)
{
    struct chef_served_install_local_session session = { 0 };
    int                                      status;

    if (transactionIdOut != NULL) {
        *transactionIdOut = 0;
    }

    status = __local_session_start(client, packagePath, proofPath, &session);
    if (status) {
        return status;
    }

    if (session.import_id == NULL || 
        session.package_resource_id == NULL || session.package_resource_id[0] == '\0' ||
        session.proof_resource_id == NULL || session.proof_resource_id[0] == '\0') {
        chef_served_install_local_session_destroy(&session);
        errno = EPROTO;
        return -1;
    }

    status = __local_session_upload(client, session.package_resource_id, packagePath);
    if (status != 0) {
        chef_served_install_local_cancel(client, NULL, session.import_id);
        chef_served_install_local_session_destroy(&session);
        return status;
    }

    status = __local_session_upload(client, session.proof_resource_id, proofPath);
    if (status != 0) {
        chef_served_install_local_cancel(client, NULL, session.import_id);
        chef_served_install_local_session_destroy(&session);
        return status;
    }

    status = __local_session_commit(client, session.import_id, transactionIdOut);
    chef_served_install_local_session_destroy(&session);
    return status;
}

static int __install_from_local(
    gracht_client_t* client,
    const char*      package_path,
    const char*      proof_path,
    unsigned int*    transactionIdOut)
{
    int status;

    status = __verify_package(package_path, proof_path);
    if (status) {
        return status;
    }
    
    status = __install_local_package(
        client,
        package_path,
        proof_path,
        transactionIdOut
    );
    if (status) {
        printf("communication error: %i\n", status);
    }
    return status;  
}

static int __install_from_store(gracht_client_t* client, struct chef_served_install_options* options, unsigned int* transactionIdOut)
{
    struct gracht_message_context context;
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

    status = chef_served_install_result(client, &context, transactionIdOut);
    if (status) {
        printf("communication error: %i\n", status);
        return status;
    }
    return 0;
}

struct __install_options {
    struct chef_served_install_options chef_options;
    int                                detach;
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
    uint64_t    revision = 0;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            __print_help();
            return 1;
        } else if (!__parse_string_switch(argv, argc, &i, "-C", 2, "--channel", 9, NULL, (char**)&options->chef_options.channel)) {
            continue;
        } else if (!__parse_string_switch(argv, argc, &i, "-P", 2, "--proof", 7, NULL, (char**)&options->proof_path)) {
            continue;
        } else if (!__parse_quantity_switch(argv, argc, &i, "-R", 2, "--revision", 10, 0, &revision)) {
            options->chef_options.revision = (int)revision;
            continue;
        } else if (!strcmp(argv[i], "-d") || !strcmp(argv[i], "--detach")) {
            options->detach = 1;
            continue;
        } else if (argv[i][0] == '-') {
            printf("unknown option: %s\n", argv[i]);
            __print_help();
            return -1;
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

    if (package == NULL) {
        return 0;
    }

    // if the package looks like a file, treat it as a local install
    if (__is_package_file(package)) {
        options->package_path = package;

        // In this case, the proof path *must* be provided
        if (options->proof_path == NULL) {
            char* defaultProofPath = __default_local_proof_path(package);
            if (defaultProofPath != NULL && __proof_file_exists(defaultProofPath)) {
                options->proof_path = defaultProofPath;
            }
            
            if (options->proof_path == NULL) {
                fprintf(stderr, "Missing local proof. Provide --proof or use --allow-unsigned in development mode.\n");
                free(defaultProofPath);
                return -1;
            }
        }
    } else {
        options->chef_options.package = platform_strdup(package);
    }

    return 0;
}

int install_main(int argc, char** argv)
{
    struct __install_options     installOptions = { 0 };
    gracht_client_t*             client;
    enum chef_transaction_result txnResult;
    unsigned int                 transactionId = 0;
    int                          status;

    // set default channel
    installOptions.chef_options.channel = "stable";

    if (argc > 1) {
        status = __parse_options(&installOptions, argc, argv);
        if (status == 1) {
            return 0;
        }
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

    // Only needed for packet based links, currently we connect unconditionally
    // as a stream-based link, meaning we are always subscribed to events.
#if 0
    // Subscribe for transaction events before issuing the install,
    // so we don't miss any notifications.
    if (!installOptions.detach) {
        status = __chef_client_subscribe(client);
        if (status) {
            fprintf(stderr, "warning: failed to subscribe for events, will not track progress\n");
        }
    }
#endif

    // is the package a path? otherwise try to download from
    // official repo
    if (installOptions.package_path != NULL) {
        status = __install_from_local(client, installOptions.package_path, installOptions.proof_path, &transactionId);
    } else {
        status = __install_from_store(client, &installOptions.chef_options, &transactionId);
    }

    if (status) {
        goto cleanup;
    }

    if (installOptions.detach) {
        printf("transaction %u submitted\n", transactionId);
        goto cleanup;
    }

    status = __chef_client_await_transaction(client, transactionId, &txnResult);
    if (status) {
        fprintf(stderr, "lost connection while waiting for transaction %u\n", transactionId);
        goto cleanup;
    }

    if (txnResult != CHEF_TRANSACTION_RESULT_SUCCESS) {
        status = -1;
    }

cleanup:
    gracht_client_shutdown(client);
    return status;
}
