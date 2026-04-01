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

#include <chef/platform.h>
#include <chef/package.h>
#include <errno.h>
#include <gracht/server.h>
#include <state.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <utils.h>
#include <vlog.h>

#include "api.h"
#include "chef_served_local_upload_service_server.h"
#include "chef_served_service_server.h"

struct __local_upload_session {
    struct __local_upload_session* next;
    char*                          import_id;
    char*                          package_resource_id;
    char*                          proof_resource_id;
    char*                          package_path;
    char*                          proof_path;
    FILE*                          package_file;
    FILE*                          proof_file;
    size_t                         package_size;
    size_t                         proof_size;
    size_t                         package_written;
    size_t                         proof_written;
    unsigned int                   package_chunk_index;
    unsigned int                   proof_chunk_index;
    int                            package_finished;
    int                            proof_finished;
};

static struct __local_upload_session* g_localUploadSessions = NULL;
static mtx_t                          g_localUploadLock;
static int                            g_localUploadLockInitialized = 0;

static int __ensure_local_upload_lock(void)
{
    if (!g_localUploadLockInitialized) {
        if (mtx_init(&g_localUploadLock, mtx_plain) != thrd_success) {
            errno = ENOMEM;
            return -1;
        }
        g_localUploadLockInitialized = 1;
    }
    return 0;
}

static char* __local_upload_path(const char* importId, const char* kind)
{
    char*  packsRoot;
    char*  uploadRoot;
    char*  path;
    size_t uploadRootLength;

    packsRoot = utils_path_local_store_root();
    if (packsRoot == NULL) {
        return NULL;
    }

    uploadRootLength = strlen(packsRoot) + strlen(CHEF_PATH_SEPARATOR_S ".uploads") + 1;
    uploadRoot = malloc(uploadRootLength);
    if (uploadRoot == NULL) {
        free(packsRoot);
        return NULL;
    }

    sprintf(uploadRoot, "%s" CHEF_PATH_SEPARATOR_S ".uploads", packsRoot);
    if (platform_mkdir(uploadRoot) != 0 && !platform_isdir(uploadRoot)) {
        free(uploadRoot);
        free(packsRoot);
        return NULL;
    }

    path = malloc(strlen(uploadRoot) + strlen(importId) + strlen(kind) + 3);
    if (path != NULL) {
        sprintf(path, "%s" CHEF_PATH_SEPARATOR_S "%s.%s", uploadRoot, importId, kind);
    }

    free(uploadRoot);
    free(packsRoot);
    return path;
}

static void __local_upload_session_destroy(struct __local_upload_session* session, int removeFiles)
{
    if (session == NULL) {
        return;
    }

    if (session->package_file != NULL) {
        fclose(session->package_file);
    }
    if (session->proof_file != NULL) {
        fclose(session->proof_file);
    }

    if (removeFiles) {
        if (session->package_path != NULL) {
            platform_unlink(session->package_path);
        }
        if (session->proof_path != NULL) {
            platform_unlink(session->proof_path);
        }
    }

    free(session->import_id);
    free(session->package_resource_id);
    free(session->proof_resource_id);
    free(session->package_path);
    free(session->proof_path);
    free(session);
}

static void __local_upload_session_remove(struct __local_upload_session* session, int removeFiles)
{
    struct __local_upload_session** link = &g_localUploadSessions;

    while (*link != NULL) {
        if (*link == session) {
            *link = session->next;
            __local_upload_session_destroy(session, removeFiles);
            return;
        }
        link = &(*link)->next;
    }
}

static struct __local_upload_session* __find_local_upload_session_by_import(const char* importId)
{
    struct __local_upload_session* session = g_localUploadSessions;

    while (session != NULL) {
        if (session->import_id != NULL && strcmp(session->import_id, importId) == 0) {
            return session;
        }
        session = session->next;
    }
    return NULL;
}

static struct __local_upload_session* __find_local_upload_session_by_resource(
    const char* resourceId,
    int*        isProofOut)
{
    struct __local_upload_session* session = g_localUploadSessions;

    while (session != NULL) {
        if (session->package_resource_id != NULL && strcmp(session->package_resource_id, resourceId) == 0) {
            if (isProofOut != NULL) {
                *isProofOut = 0;
            }
            return session;
        }
        if (session->proof_resource_id != NULL && strcmp(session->proof_resource_id, resourceId) == 0) {
            if (isProofOut != NULL) {
                *isProofOut = 1;
            }
            return session;
        }
        session = session->next;
    }
    return NULL;
}

static int __validate_package_name(const char* name)
{
    char** names = utils_split_package_name(name);
    if (names == NULL) {
        return -1;
    }

    strsplit_free(names);
    return 0;
}

static int __next_local_revision(const char* name)
{
    struct state_application* applications;
    struct state_transaction* transactions;
    int                       applicationsCount = 0;
    int                       transactionsCount = 0;
    int                       revision = 0;

    if (served_state_get_applications(&applications, &applicationsCount) == 0) {
        for (int i = 0; i < applicationsCount; ++i) {
            struct state_application* application = &applications[i];

            if (application->name == NULL || strcmp(application->name, name) != 0) {
                continue;
            }

            for (int j = 0; j < application->revisions_count; ++j) {
                struct chef_version* version = application->revisions[j].version;
                if (version != NULL && version->revision < revision) {
                    revision = version->revision;
                }
            }
        }
    }

    if (served_state_get_transaction_states(&transactions, &transactionsCount) == 0) {
        for (int i = 0; i < transactionsCount; ++i) {
            if (transactions[i].name != NULL && strcmp(transactions[i].name, name) == 0 &&
                transactions[i].revision < revision) {
                revision = transactions[i].revision;
            }
        }
    }

    return revision == 0 ? -1 : revision - 1;
}

static int __stage_local_install(
    const char* sourcePath,
    const char* sourceProofPath,
    const char* publisher,
    const char* package,
    int         revision)
{
    char* packPath;
    char* proofPath;
    int   status;

    packPath = utils_path_local_pack(publisher, package, revision);
    proofPath = utils_path_local_proof(publisher, package, revision);
    if (packPath == NULL || proofPath == NULL) {
        free(packPath);
        free(proofPath);
        return -1;
    }

    status = platform_copyfile(sourcePath, packPath);
    if (status != 0) {
        VLOG_ERROR("api", "failed to stage local package %s -> %s\n", sourcePath, packPath);
        free(proofPath);
        free(packPath);
        return -1;
    }

    if (sourceProofPath != NULL && sourceProofPath[0] != '\0') {
        status = platform_copyfile(sourceProofPath, proofPath);
        if (status != 0) {
            VLOG_ERROR("api", "failed to stage local proof %s -> %s\n", sourceProofPath, proofPath);
            remove(packPath);
            free(proofPath);
            free(packPath);
            return -1;
        }
    }

    free(proofPath);
    free(packPath);
    return 0;
}

static int __prepare_local_install(
    const char*                               packagePath,
    const char*                               proofPath,
    char**                                    packageNameOut,
    int*                                      revisionOut)
{
    struct chef_package*       package = NULL;
    struct chef_package_proof* proof = NULL;
    char*                      fullName = NULL;
    const char*                publisher = NULL;
    int                        revision;
    int                        status = -1;

    if (packageNameOut == NULL || revisionOut == NULL) {
        errno = EINVAL;
        return -1;
    }

    *packageNameOut = NULL;

    status = chef_package_load(packagePath, &package, NULL, NULL, NULL);
    if (status != 0) {
        return -1;
    }

    if (proofPath != NULL && proofPath[0] != '\0') {
        status = utils_load_local_package_proof(proofPath, &proof);
        if (status != 0) {
            goto cleanup;
        }
        publisher = proof->identity;
    } else {
        errno = ENOENT;
        goto cleanup;
    }

    fullName = malloc(strlen(publisher) + strlen(package->package) + 2);
    if (fullName == NULL) {
        goto cleanup;
    }

    sprintf(fullName, "%s/%s", publisher, package->package);
    if (__validate_package_name(fullName) != 0) {
        goto cleanup;
    }

    served_state_lock();
    revision = __next_local_revision(fullName);

    status = __stage_local_install(packagePath, proofPath, publisher, package->package, revision);
    if (status != 0) {
        served_state_unlock();
        goto cleanup;
    }

    served_state_unlock();
    *packageNameOut = fullName;
    *revisionOut = revision;
    status = 0;

cleanup:
    chef_package_proof_free(proof);
    chef_package_free(package);
    if (status != 0) {
        free(fullName);
    }
    return status;
}

void chef_served_install_local_begin_invocation(struct gracht_message* message, const struct chef_served_install_local_options* options)
{
    struct __local_upload_session*           session = NULL;
    struct chef_served_install_local_session response = { 0 };

    if (__ensure_local_upload_lock() != 0 || options->package_size == 0) {
        chef_served_install_local_begin_response(message, &response);
        return;
    }

    if (options->proof_size == 0) {
        chef_served_install_local_begin_response(message, &response);
        return;
    }

    session = calloc(1, sizeof(struct __local_upload_session));
    if (session == NULL) {
        chef_served_install_local_begin_response(message, &response);
        return;
    }

    session->import_id = platform_secure_random_string_new(24);
    session->package_resource_id = platform_secure_random_string_new(24);
    session->proof_resource_id = options->proof_size > 0 ? platform_secure_random_string_new(24) : NULL;
    session->package_path = session->import_id != NULL ? __local_upload_path(session->import_id, "pack") : NULL;
    session->proof_path = (options->proof_size > 0 && session->import_id != NULL) ? __local_upload_path(session->import_id, "proof") : NULL;
    session->package_size = options->package_size;
    session->proof_size = options->proof_size;

    if (session->import_id == NULL || session->package_resource_id == NULL || session->package_path == NULL ||
        (options->proof_size > 0 && (session->proof_resource_id == NULL || session->proof_path == NULL))) {
        __local_upload_session_destroy(session, 1);
        chef_served_install_local_begin_response(message, &response);
        return;
    }

    session->package_file = fopen(session->package_path, "wb");
    if (session->package_file == NULL) {
        __local_upload_session_destroy(session, 1);
        chef_served_install_local_begin_response(message, &response);
        return;
    }

    if (options->proof_size > 0) {
        session->proof_file = fopen(session->proof_path, "wb");
        if (session->proof_file == NULL) {
            __local_upload_session_destroy(session, 1);
            chef_served_install_local_begin_response(message, &response);
            return;
        }
    }

    mtx_lock(&g_localUploadLock);
    session->next = g_localUploadSessions;
    g_localUploadSessions = session;
    mtx_unlock(&g_localUploadLock);

    response.import_id = session->import_id;
    response.package_resource_id = session->package_resource_id;
    response.proof_resource_id = session->proof_resource_id;
    chef_served_install_local_begin_response(message, &response);
}

void chef_served_local_upload_open_invocation(struct gracht_message* message, const char* resource_id, const unsigned int size)
{
    struct __local_upload_session* session;
    int                            isProof = 0;

    if (__ensure_local_upload_lock() != 0 || resource_id == NULL) {
        chef_served_local_upload_open_response(message, "");
        return;
    }

    mtx_lock(&g_localUploadLock);
    session = __find_local_upload_session_by_resource(resource_id, &isProof);
    if (session == NULL || (size_t)size != (isProof ? session->proof_size : session->package_size)) {
        mtx_unlock(&g_localUploadLock);
        chef_served_local_upload_open_response(message, "");
        return;
    }
    mtx_unlock(&g_localUploadLock);
    chef_served_local_upload_open_response(message, (char*)resource_id);
}

void chef_served_local_upload_write_chunk_invocation(struct gracht_message* message, const char* session_id, const unsigned int index, const uint8_t* data, const uint32_t data_count)
{
    struct __local_upload_session* session;
    FILE*                          file;
    size_t*                        written;
    size_t                         totalSize;
    unsigned int*                  expectedIndex;
    int                            isProof = 0;

    (void)message;

    if (__ensure_local_upload_lock() != 0 || session_id == NULL || data == NULL) {
        return;
    }

    mtx_lock(&g_localUploadLock);
    session = __find_local_upload_session_by_resource(session_id, &isProof);
    if (session == NULL) {
        mtx_unlock(&g_localUploadLock);
        return;
    }

    file = isProof ? session->proof_file : session->package_file;
    written = isProof ? &session->proof_written : &session->package_written;
    totalSize = isProof ? session->proof_size : session->package_size;
    expectedIndex = isProof ? &session->proof_chunk_index : &session->package_chunk_index;

    if (file == NULL || index != *expectedIndex || *written + data_count > totalSize ||
        fwrite(data, 1, data_count, file) != data_count) {
        mtx_unlock(&g_localUploadLock);
        return;
    }

    *written += data_count;
    (*expectedIndex)++;
    mtx_unlock(&g_localUploadLock);
}

void chef_served_local_upload_finish_invocation(struct gracht_message* message, const char* session_id)
{
    struct __local_upload_session* session;
    FILE**                         file;
    size_t                         written;
    size_t                         totalSize;
    int*                           finished;
    int                            isProof = 0;

    (void)message;

    if (__ensure_local_upload_lock() != 0 || session_id == NULL) {
        return;
    }

    mtx_lock(&g_localUploadLock);
    session = __find_local_upload_session_by_resource(session_id, &isProof);
    if (session == NULL) {
        mtx_unlock(&g_localUploadLock);
        return;
    }

    file = isProof ? &session->proof_file : &session->package_file;
    written = isProof ? session->proof_written : session->package_written;
    totalSize = isProof ? session->proof_size : session->package_size;
    finished = isProof ? &session->proof_finished : &session->package_finished;

    if (*file != NULL && written == totalSize) {
        fclose(*file);
        *file = NULL;
        *finished = 1;
    }
    mtx_unlock(&g_localUploadLock);
}

void chef_served_local_upload_close_invocation(struct gracht_message* message, const char* session_id)
{
    (void)message;
    (void)session_id;
}

void chef_served_install_local_end_invocation(struct gracht_message* message, const char* import_id)
{
    struct __local_upload_session* session;
    char*                          packageName = NULL;
    int                            revision = 0;
    unsigned int                   transactionId = 0;

    VLOG_DEBUG("api", "chef_served_install_local_end_invocation(import_id=%s)\n",
               import_id ? import_id : "(null)");

    if (__ensure_local_upload_lock() != 0 || import_id == NULL) {
        chef_served_install_local_end_response(message, 0);
        return;
    }

    mtx_lock(&g_localUploadLock);
    session = __find_local_upload_session_by_import(import_id);
    if (session == NULL || !session->package_finished || !session->proof_finished) {
        mtx_unlock(&g_localUploadLock);
        chef_served_install_local_end_response(message, 0);
        return;
    }

    if (__prepare_local_install(
            session->package_path,
            session->proof_finished ? session->proof_path : NULL,
            &packageName,
            &revision) == 0) {
        // served_api_create_install_transaction will check whether the
        // application is already installed and schedule an update
        // transaction instead of an install when appropriate.
        transactionId = served_api_create_install_transaction(packageName, NULL, revision);
        free(packageName);
    }

    __local_upload_session_remove(session, 1);
    mtx_unlock(&g_localUploadLock);
    chef_served_install_local_end_response(message, transactionId);
}

void chef_served_install_local_cancel_invocation(struct gracht_message* message, const char* import_id)
{
    struct __local_upload_session* session;

    (void)message;

    if (__ensure_local_upload_lock() != 0 || import_id == NULL) {
        return;
    }

    mtx_lock(&g_localUploadLock);
    session = __find_local_upload_session_by_import(import_id);
    if (session != NULL) {
        __local_upload_session_remove(session, 1);
    }
    mtx_unlock(&g_localUploadLock);
}
