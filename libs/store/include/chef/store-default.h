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

#ifndef __LIBSTORE_DEFAULT_H__
#define __LIBSTORE_DEFAULT_H__

#include <chef/client.h>
#include <chef/api/package.h>
#include <chef/api/account.h>
#include <chef/platform.h>
#include <chef/store.h>
#include <errno.h>
#include <jansson.h>
#include <stdlib.h>
#include <string.h>
#include <vlog.h>

static char** __split_name(const char* name)
{
    // split the publisher/package
    int    namesCount = 0;
    char** names = strsplit(name, '/');
    if (names == NULL) {
        VLOG_ERROR("store", "__find_package_in_inventory: invalid package naming '%s' (must be publisher/package)\n", name);
        return NULL;
    }

    while (names[namesCount] != NULL) {
        namesCount++;
    }

    if (namesCount != 2) {
        VLOG_ERROR("store", "__find_package_in_inventory: invalid package naming '%s' (must be publisher/package)\n", name);
        strsplit_free(names);
        return NULL;
    }
    return names;
}

static int store_default_resolve_package(struct store_package* package, const char* path, struct chef_observer* observer, int* revisionDownloaded)
{
    struct chef_download_params downloadParams;
    int                         status;
    char**                      names;
    VLOG_DEBUG("chef", "store_default_resolve_package()\n");

    // split name into publisher/package
    names = __split_name(package->name);
    if (names == NULL) {
        VLOG_ERROR("chef", "store_default_resolve_package: invalid package name '%s'\n",
            package->name);
        return -1;
    }

    // initialize download params
    downloadParams.publisher = names[0];
    downloadParams.package   = names[1];
    downloadParams.platform  = package->platform;
    downloadParams.arch      = package->arch;
    downloadParams.channel   = package->channel;
    downloadParams.revision  = 0;
    downloadParams.observer  = observer;

    status = chefclient_pack_download(&downloadParams, path);
    if (status == 0) {
        *revisionDownloaded = downloadParams.revision;
    }
    strsplit_free(names);
    return status;
}

static char** __split_package_key(const char* key)
{
    // split the publisher/package/revision
    int    namesCount = 0;
    char** names = strsplit(key, '/');
    if (names == NULL) {
        VLOG_ERROR("chef", "__split_package_key: invalid package key '%s'\n", key);
        return NULL;
    }

    while (names[namesCount] != NULL) {
        namesCount++;
    }

    if (namesCount != 3) {
        VLOG_ERROR("chef", "__split_package_key: invalid package key '%s'\n", key);
        strsplit_free(names);
        return NULL;
    }
    return names;
}

static int __parse_proof_origin(const char* origin, enum chef_package_proof_origin* originOut)
{
    if (origin == NULL || originOut == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (strcmp(origin, "developer") == 0) {
        *originOut = CHEF_PACKAGE_PROOF_ORIGIN_DEVELOPER;
        return 0;
    }

    if (strcmp(origin, "publisher") == 0 || strcmp(origin, "store") == 0) {
        *originOut = CHEF_PACKAGE_PROOF_ORIGIN_STORE;
        return 0;
    }

    VLOG_ERROR("chef", "__parse_proof_origin: unsupported proof origin '%s'\n", origin);
    errno = ENOTSUP;
    return -1;
}

static void __cleanup_package_proof(struct store_proof_package* proof)
{
    if (proof == NULL) {
        return;
    }

    free((void*)proof->proof.identity);
    free((void*)proof->proof.hash_algorithm);
    free((void*)proof->proof.hash);
    free((void*)proof->proof.public_key);
    free((void*)proof->proof.signature);
    memset(&proof->proof, 0, sizeof(proof->proof));
}

static int __copy_required_json_string(json_t* root, const char* key, const char** valueOut)
{
    json_t* member;

    member = json_object_get(root, key);
    if (member == NULL || !json_is_string(member)) {
        VLOG_ERROR("chef", "__copy_required_json_string: missing string member '%s'\n", key);
        errno = EINVAL;
        return -1;
    }

    *valueOut = platform_strdup(json_string_value(member));
    if (*valueOut == NULL) {
        VLOG_ERROR("chef", "__copy_required_json_string: failed to duplicate '%s'\n", key);
        return -1;
    }
    return 0;
}

static int __parse_package_proof_stream(FILE* stream, const char* key, union store_proof* proof)
{
    json_t*      root;
    json_t*      origin;
    json_error_t error;
    int          status = -1;

    root = json_loadf(stream, 0, &error);
    if (root == NULL) {
        VLOG_ERROR(
            "chef",
            "__parse_package_proof_stream: failed to parse proof '%s' at line %d, column %d: %s\n",
            key,
            error.line,
            error.column,
            error.text
        );
        errno = EINVAL;
        return -1;
    }

    proof->package.header.type = STORE_PROOF_PACKAGE;
    snprintf(proof->package.header.key, sizeof(proof->package.header.key), "%s", key);
    memset(&proof->package.proof, 0, sizeof(proof->package.proof));

    origin = json_object_get(root, "origin");
    if (origin == NULL || !json_is_string(origin)) {
        VLOG_ERROR("chef", "__parse_package_proof_stream: missing string member 'origin'\n");
        errno = EINVAL;
        goto cleanup;
    }

    status = __parse_proof_origin(json_string_value(origin), &proof->package.proof.origin);
    if (status != 0) {
        goto cleanup;
    }

    status = __copy_required_json_string(root, "identity", &proof->package.proof.identity);
    if (status != 0) {
        goto cleanup;
    }

    status = __copy_required_json_string(root, "hash-algorithm", &proof->package.proof.hash_algorithm);
    if (status != 0) {
        goto cleanup;
    }

    status = __copy_required_json_string(root, "hash", &proof->package.proof.hash);
    if (status != 0) {
        goto cleanup;
    }

    status = __copy_required_json_string(root, "public-key", &proof->package.proof.public_key);
    if (status != 0) {
        goto cleanup;
    }

    status = __copy_required_json_string(root, "signature", &proof->package.proof.signature);
    if (status != 0) {
        goto cleanup;
    }

    status = 0;

cleanup:
    if (status != 0) {
        __cleanup_package_proof(&proof->package);
    }
    json_decref(root);
    return status;
}

static int store_default_resolve_proof(enum store_proof_type keyType, const char* key, struct chef_observer* observer, union store_proof* proof)
{
    int status;
    VLOG_DEBUG("chef", "store_default_resolve_proof()\n");
    (void)observer;
    
    switch (keyType) {
        // for publisher, key is publisher name
        case STORE_PROOF_PUBLISHER: {
            struct chef_publisher* publisher = NULL;
            status = chef_account_publisher_get(key, &publisher);
            if (status != 0) {
                VLOG_ERROR("chef", "store_default_resolve_proof: failed to get publisher '%s'\n", key);
                return -1;
            }
            
            proof->publisher.header.type = STORE_PROOF_PUBLISHER;
            snprintf(proof->publisher.header.key, sizeof(proof->publisher.header.key), "%s", key);
            proof->publisher.public_key = platform_strdup(chef_publisher_public_key(publisher));
            proof->publisher.signed_key = platform_strdup(chef_publisher_signed_key(publisher));
            chef_publisher_free(publisher);
            break;
        }
        
        // for package, key is publisher/package/revision
        case STORE_PROOF_PACKAGE: {
            struct chef_proof_params proofParams;
            char**                   names;
            FILE*                    stream;
            
            names = __split_package_key(key);
            if (names == NULL) {
                VLOG_ERROR("chef", "store_default_resolve_proof: invalid package proof key '%s'\n", key);
                return -1;
            }

            stream = tmpfile();
            if (stream == NULL) {
                VLOG_ERROR("chef", "store_default_resolve_proof: failed to create temporary file for package proof\n");
                strsplit_free(names);
                return -1;
            }

            proofParams.publisher = names[0];
            proofParams.package   = names[1];
            proofParams.revision  = atoi(names[2]);
            status = chefclient_pack_proof(&proofParams, stream);
            strsplit_free(names);

            if (status != 0) {
                VLOG_ERROR("chef", "store_default_resolve_proof: failed to get package proof '%s'\n", key);
                fclose(stream);
                return -1;
            }

            rewind(stream);
            status = __parse_package_proof_stream(stream, key, proof);
            fclose(stream);

            if (status != 0) {
                VLOG_ERROR("chef", "store_default_resolve_proof: failed to parse package proof '%s'\n", key);
                return -1;
            }

            break;
        }
        default:
            VLOG_ERROR("chef", "store_default_resolve_proof: invalid proof type %d\n", keyType);
            return -1;
    }
    return 0;
}

const static struct store_backend g_store_default_backend = {
    .resolve_package = store_default_resolve_package,
    .resolve_proof = store_default_resolve_proof
};

#endif //!__LIBSTORE_DEFAULT_H__
