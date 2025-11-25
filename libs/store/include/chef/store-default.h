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
#include <stdlib.h>
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

static char* __read_proof(FILE* stream)
{
    char buffer[4096];
    size_t readBytes = fread(&buffer[0], 1, sizeof(buffer) - 1, stream);
    if (readBytes == 0) {
        VLOG_ERROR("chef", "__read_proof: failed to read package proof data\n");
        return NULL;
    }
    buffer[readBytes] = '\0';
    return platform_strdup(&buffer[0]);
}

static int store_default_resolve_proof(enum store_proof_type keyType, const char* key, struct chef_observer* observer, union store_proof* proof)
{
    int status;
    VLOG_DEBUG("chef", "store_default_resolve_proof()\n");
    
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
                return -1;
            }

            // rewind and read the signature
            rewind(stream);
            proof->package.header.type = STORE_PROOF_PACKAGE;
            snprintf(proof->package.header.key, sizeof(proof->package.header.key), "%s", key);
            proof->package.signature = __read_proof(stream);
            fclose(stream);

            if (proof->package.signature == NULL) {
                VLOG_ERROR("chef", "store_default_resolve_proof: failed to read package proof data for '%s'\n", key);
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
