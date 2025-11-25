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

#ifndef __LIBSTORE_H__
#define __LIBSTORE_H__

#include <chef/list.h>
#include <chef/observer.h>
#include <stdio.h>

enum store_proof_type {
    STORE_PROOF_PUBLISHER,
    STORE_PROOF_PACKAGE
};

static int proof_format_publisher_key(char* buffer, size_t length, const char* publisher) {
    return snprintf(&buffer[0], length, "%s", publisher);
}

static int proof_format_package_key(char* buffer, size_t length, const char* publisher, const char* package, int revision) {
    return snprintf(&buffer[0], length, "%s/%s/%i", publisher, package, revision);
}

struct store_proof_header {
    enum store_proof_type type;
    char                  key[128];
};

struct store_proof_publisher {
    struct store_proof_header header;
    const char*               public_key;
    const char*               signed_key;
};

struct store_proof_package {
    struct store_proof_header header;
    const char*               signature;
};

union store_proof {
    struct store_proof_header    header;
    struct store_proof_publisher publisher;
    struct store_proof_package   package;
};

struct store_package {
    // Name of the package is formatted as publisher/package
    const char* name;
    // The platform and architecture specific build of the package.
    // These are optional, and will default to the arch/platform specified
    // for the store instnace. Only provide these to override.
    const char* platform;
    const char* arch;
    // Channel, if specified, will refer to the channel that should be
    // resolved from. If no channel is specified, then the revision must.
    const char* channel;
    // If revision is set, then channel will be ignored if set.
    int         revision;
};

struct store_backend {
    int (*resolve_package)(struct store_package* package, const char* path, struct chef_observer* observer, int* revisionDownloaded);
    int (*resolve_proof)(enum store_proof_type keyType, const char* key, struct chef_observer* observer, union store_proof* proof);
};

struct store_parameters {
    const char*          platform;
    const char*          architecture;
    struct store_backend backend;
};

/**
 * @brief 
 * store
 * @return int 
 */
extern int store_initialize(struct store_parameters* parameters);

/**
 * @brief 
 */
extern void store_cleanup(void);

/**
 * @brief Stores the given package, making sure we have a local copy of it in
 * our local store.
 * 
 * @param[In] package Options describing the package that should be fetched from store.
 * @return int 
 */
extern int store_ensure_package(struct store_package* package, struct chef_observer* observer);

/**
 * @brief Retrieves the path of an package based on it's parameters. It must be already
 * present in the local store.
 * 
 * @param[In]  package Options describing the package from the store.
 * @param[Out] pathOut Returns a zero-terminated string with the path of the package. This
 *                     string should not be freed. It will be valid until store_cleanup is called.
 * @return int  
 */
extern int store_package_path(struct store_package* package, const char** pathOut);

/**
 * @brief Ensures the proof identified by the parameters exists in the local database.
 * 
 * @param[In]  keyType  The proof key type, this will determine the expected format of the key.
 * @param[In]  key      The proof key.
 * @return int          0 On success, -1 on error. Consult errno for details.
 */
extern int store_proof_ensure(enum store_proof_type keyType, const char* key, struct chef_observer* observer);

/**
 * @brief Retrieves the proof based on it's key. If the proof does not exist, the backend
 * will attempt to resolve it first.
 * 
 * @param[In]  keyType     The proof key type, this will determine the expected format of the key.
 * @param[In]  key         The proof key.
 * @param[In]  proofBuffer Buffer to store the proof data in. This should match one of the store_proof_* structures.
 * @return int             0 On success, -1 on error. Consult errno for details.
 */
extern int store_proof_lookup(enum store_proof_type keyType, const char* key, void* proof);

#endif //!__LIBSTORE_H__
