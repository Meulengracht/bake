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
#include "inventory.h"
#include <chef/platform.h>
#include <chef/store.h>
#include <jansson.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <vlog.h>

struct __proof_header {
    enum store_proof_type type;
    const char            key[128];
};

struct __proof_publisher {
    struct __proof_header header;
    char                  public_key[4096];
    char                  signed_key[4096];
};

struct __proof_package {
    struct store_proof_header header;
    char                      signature[4096];
};

union __proof {
    struct __proof_header    header;
    struct __proof_publisher publisher;
    struct __proof_package   package;
};

struct store_inventory_pack {
    const char* path;
    const char* publisher;
    const char* package;
    const char* platform;
    const char* arch;
    const char* channel;
    int         revision;
};

struct store_inventory {
    const char*                  path;
    struct timespec              last_check;
    struct store_inventory_pack* packs;
    int                          packs_count;
    union __proof*               proofs;
    int                          proofs_count;
};

static struct store_inventory* __inventory_new(void)
{
    struct store_inventory* inventory;

    inventory = (struct store_inventory*)malloc(sizeof(struct store_inventory));
    if (inventory == NULL) {
        return NULL;
    }

    memset(inventory, 0, sizeof(struct store_inventory));
    return inventory;
}

static struct timespec __parse_timespec(const char* timestamp)
{
    struct timespec ts;
    return ts;
}

static int __parse_packs(struct store_inventory* inventory, json_t* packs)
{
    size_t count = json_array_size(packs);
    VLOG_DEBUG("inventory", "__parse_packs()\n");

    if (count == 0) {
        return 0;
    }
    
    inventory->packs = (struct store_inventory_pack*)calloc(count, sizeof(struct store_inventory_pack));
    if (inventory->packs == NULL) {
        return -1;
    }
    inventory->packs_count = count;
    
    for (size_t i = 0; i < count; i++) {
        json_t* pack = json_array_get(packs, i);
        json_t* path = json_object_get(pack, "path");
        json_t* publisher = json_object_get(pack, "publisher");
        json_t* name = json_object_get(pack, "package");
        json_t* platform = json_object_get(pack, "platform");
        json_t* architecture = json_object_get(pack, "architecture");
        json_t* channel = json_object_get(pack, "channel");
        json_t* revision = json_object_get(pack, "revision");

        inventory->packs[i].path = platform_strdup(json_string_value(path));
        inventory->packs[i].publisher = platform_strdup(json_string_value(publisher));
        inventory->packs[i].package = platform_strdup(json_string_value(name));
        inventory->packs[i].platform = platform_strdup(json_string_value(platform));
        inventory->packs[i].arch = platform_strdup(json_string_value(architecture));
        inventory->packs[i].channel = platform_strdup(json_string_value(channel));
        inventory->packs[i].revision = json_integer_value(revision);
    }
    return 0;
}

static int __parse_publisher_proof(struct __proof_publisher* proof, json_t* root)
{
    json_t* pkey = json_object_get(root, "public-key");
    size_t  pkeyLength;
    json_t* skey = json_object_get(root, "signed-key");
    size_t  skeyLength;

    // both are required
    if (pkey == NULL || skey == NULL) {
        VLOG_ERROR("inventory", "__parse_publisher_proof: invalid proof entry\n");
        return -1;
    }

    pkeyLength = strlen(json_string_value(pkey));
    skeyLength = strlen(json_string_value(skey));

    // length check to ensure we are not hitting some bad input
    if (pkeyLength >= sizeof(proof->public_key) || skeyLength >= sizeof(proof->signed_key)) {
        VLOG_ERROR("inventory", "__parse_publisher_proof: corrupted proof entry\n");
        return -1;
    }

    memcpy(&proof->public_key[0], json_string_value(pkey), pkeyLength);
    memcpy(&proof->signed_key[0], json_string_value(skey), skeyLength);
    return 0;
}

static int __parse_package_proof(struct __proof_package* proof, json_t* root)
{
    json_t* signature = json_object_get(root, "signature");
    size_t  signatureLength;
    
    if (signature == NULL) {
        VLOG_ERROR("inventory", "__parse_package_proof: invalid proof entry\n");
        return -1;
    }

    signatureLength = strlen(json_string_value(signature));

    if (signatureLength >= sizeof(proof->signature)) {
        VLOG_ERROR("inventory", "__parse_package_proof: corrupted proof entry\n");
        return -1;
    }

    memcpy(&proof->signature[0], json_string_value(signature), signatureLength);
    return 0;
}

static int __parse_proofs(struct store_inventory* inventory, json_t* proofs)
{
    size_t count = json_array_size(proofs);
    VLOG_DEBUG("inventory", "__parse_proofs()\n");

    if (count == 0) {
        return 0;
    }
    
    inventory->proofs = (union __proof*)calloc(count, sizeof(union __proof));
    if (inventory->proofs == NULL) {
        VLOG_ERROR("inventory", "__parse_proofs: failed to allocate memory for proofs\n");
        return -1;
    }
    inventory->proofs_count = count;
    
    for (size_t i = 0; i < count; i++) {
        json_t* proof = json_array_get(proofs, i);
        if (proof == NULL) {
            VLOG_ERROR("inventory", "__parse_proofs: invalid proof entry %i\n", i);
            return -1;
        }

        json_t* type = json_object_get(proof, "type");
        json_t* key = json_object_get(proof, "key");
        if (type == NULL || key == NULL) {
            VLOG_ERROR("inventory", "__parse_proofs: invalid proof entry %i\n", i);
            return -1;
        }

        inventory->proofs[i].header.type = (enum store_proof_type)json_integer_value(type);
        memcpy(&inventory->proofs[i].header.key[0], json_string_value(key), strlen(json_string_value(key)));
        switch (inventory->proofs[i].header.type) {
            case STORE_PROOF_PUBLISHER:
                if (__parse_publisher_proof(&inventory->proofs[i].publisher, proof)) {
                    VLOG_ERROR("inventory", "__parse_proofs: failed to parse publisher proof (index %i) from inventory\n", i);
                    return -1;
                };
                break;
            case STORE_PROOF_PACKAGE:
                if (__parse_package_proof(&inventory->proofs[i].package, proof)) {
                    VLOG_ERROR("inventory", "__parse_proofs: failed to parse package proof (index %i) from inventory\n", i);
                    return -1;
                };
                break;
        }
    }
    return 0;
}

static int __parse_inventory(const char* json, struct store_inventory** inventoryOut)
{
    struct store_inventory* inventory;
    json_error_t             error;
    json_t*                  root;
    json_t*                  last_check;
    json_t*                  member;
    int                      status;
    VLOG_DEBUG("inventory", "__parse_inventory()\n");

    // instantiate a new inventory
    inventory = __inventory_new();
    if (json == NULL) {
        status = 0;
        goto exit;
    }

    root = json_loads(json, 0, &error);
    if (!root) {
        status = 0;
        goto exit;
    }

    // parse the root members
    last_check = json_object_get(root, "last_check");
    if (last_check) {
        inventory->last_check = __parse_timespec(json_string_value(last_check));
    }

    member = json_object_get(root, "packs");
    if (member != NULL) {
        status = __parse_packs(inventory, member);
        if (status) {
            goto exit;
        }
    }

    member = json_object_get(root, "proofs");
    if (member != NULL) {
        status = __parse_proofs(inventory, member);
        if (status) {
            goto exit;
        }
    }

exit:
    *inventoryOut = inventory;
    return 0;
}

static int __inventory_load_file(const char* path, char** jsonOut)
{
    FILE* file;
    long  size;
    char* json = NULL;
    VLOG_DEBUG("inventory", "__inventory_load_file(path=%s)\n", path);

    file = fopen(path, "r+");
    if (file == NULL) {
        file = fopen(path, "w+");
        if (file == NULL) {
            return -1;
        }
    }

    fseek(file, 0, SEEK_END);
    size = ftell(file);
    fseek(file, 0, SEEK_SET);

    if (size) {
        size_t bytesRead;

        json = (char*)malloc(size + 1); // sz?!
        if (!json) {
            fclose(file);
            return -1;
        }
        memset(json, 0, size + 1);
        bytesRead = fread(json, 1, size, file);
        if (bytesRead != size) {
            VLOG_ERROR("inventory", "__inventory_load_file: failed to read file: %s\n", strerror(errno));
            fclose(file);
            return -1;
        }
    }

    fclose(file);
    *jsonOut = json;
    return 0;
}

int inventory_load(const char* path, struct store_inventory** inventoryOut)
{
    struct store_inventory* inventory;
    int                      status;
    char*                    json;
    char*                    filePath;
    VLOG_DEBUG("inventory", "inventory_load(path=%s)\n", path);
    
    if (path == NULL || inventoryOut == NULL) {
        errno = EINVAL;
        return -1;
    }

    filePath = strpathcombine(path, "inventory.json");
    if (filePath == NULL) {
        VLOG_ERROR("inventory", "inventory_load: failed to allocate memory for path\n");
        return -1;
    }

    status = __inventory_load_file(filePath, &json);
    if (status) {
        free(filePath);
        VLOG_ERROR("inventory", "inventory_load: failed to load %s\n", filePath);
        return -1;
    }
    free(filePath);

    status = __parse_inventory(json, &inventory);
    free(json);

    if (status) {
        VLOG_ERROR("inventory", "inventory_load: failed to parse the inventory, file corrupt??\n");
        return -1;
    }
    VLOG_TRACE("inventory", "inventory loaded, %i packs available\n", inventory->packs_count);

    // store the base path of the inventory
    inventory->path = platform_strdup(path);
    *inventoryOut = inventory;
    return 0;
}

int inventory_get_pack(struct store_inventory* inventory, const char* publisher, 
    const char* package, const char* platform, const char* arch, const char* channel,
    int revision, struct store_inventory_pack** packOut)
{
    struct store_inventory_pack* result = NULL;
    VLOG_DEBUG("inventory", "inventory_get_pack()\n");

    if (inventory == NULL || publisher == NULL || package == NULL) { 
        errno = EINVAL;
        return -1;
    }

    for (int i = 0; i < inventory->packs_count; i++) {
        if (strcmp(inventory->packs[i].publisher, publisher) == 0 &&
            strcmp(inventory->packs[i].package, package) == 0 &&
            strcmp(inventory->packs[i].platform, platform) == 0 &&
            strcmp(inventory->packs[i].arch, arch) == 0) {
            if (channel == NULL) {
                if (revision == 0) {
                    if (result) {
                        if (inventory->packs[i].revision > result->revision) {
                            result = &inventory->packs[i];
                        }
                    } else {
                        result = &inventory->packs[i];
                    }
                } else if (revision == inventory->packs[i].revision) {
                    *packOut = &inventory->packs[i];
                    return 0;
                }
            } else if (strcmp(inventory->packs[i].channel, channel) == 0) {
                *packOut = &inventory->packs[i];
                return 0;
            }
        }
    }

    if (result) {
        *packOut = result;
        return 0;
    }

    errno = ENOENT;
    return -1;
}

int inventory_add(struct store_inventory* inventory, const char* packPath, const char* publisher,
    const char* package, const char* platform, const char* arch, const char* channel,
    int revision, struct store_inventory_pack** packOut)
{
    struct store_inventory_pack* packEntry;
    void*                         newArray;
    void*                         oldArray;
    VLOG_DEBUG("inventory", "inventory_add(path=%s, publisher=%s, package=%s)\n", 
        packPath, publisher, package);

    if (inventory == NULL || publisher == NULL || package == NULL) {
        errno = EINVAL;
        return -1;
    }

    // extend the pack array by one
    oldArray = inventory->packs;
    newArray = malloc(sizeof(struct store_inventory_pack) * (inventory->packs_count + 1));
    if (!newArray) {
        return -1;
    }

    if (inventory->packs_count) {
        memcpy(newArray, inventory->packs, sizeof(struct store_inventory_pack) * inventory->packs_count);
    }

    // now create the new entry
    packEntry = &((struct store_inventory_pack*)newArray)[inventory->packs_count];
    memset(packEntry, 0, sizeof(struct store_inventory_pack));

    packEntry->path      = platform_strdup(packPath);
    packEntry->publisher = platform_strdup(publisher);
    packEntry->package   = platform_strdup(package);
    packEntry->platform  = platform != NULL ? platform_strdup(platform) : NULL;
    packEntry->arch      = platform != NULL ? platform_strdup(arch) : NULL;
    packEntry->channel   = platform_strdup(channel);
    packEntry->revision  = revision;

    *packOut = packEntry;

    // Update the new array stored before we serialize the inventory to disk.
    inventory->packs = newArray;
    inventory->packs_count += 1;
    free(oldArray);
    return 0;
}

static void __to_store_version(union __proof* ip, union store_proof* sp)
{
    sp->header.type = ip->header.type;
    memcpy(&sp->header.key[0], &ip->header.key[0], sizeof(ip->header.key));

    switch (ip->header.type) {
        case STORE_PROOF_PUBLISHER:
            sp->publisher.public_key = &ip->publisher.public_key[0];
            sp->publisher.signed_key = &ip->publisher.signed_key[0];
            break;
        case STORE_PROOF_PACKAGE:
            sp->package.signature = &ip->package.signature[0];
            break;
    }
}

int inventory_get_proof(struct store_inventory* inventory, enum store_proof_type keyType, const char* key, union store_proof* proof)
{
    VLOG_DEBUG("inventory", "inventory_get_proof(key=%s)\n", key);

    if (inventory == NULL || key == NULL) { 
        errno = EINVAL;
        return -1;
    }

    for (int i = 0; i < inventory->proofs_count; i++) {
        if (strcmp(&inventory->proofs[i].header.key[0], key) == 0) {
            __to_store_version(&inventory->proofs[i], proof);
            return 0;
        }
    }

    errno = ENOENT;
    return -1;
}

int inventory_add_proof(struct store_inventory* inventory, union store_proof* proof)
{
    union store_proof* entry;
    void*              newArray;
    void*              oldArray;
    VLOG_DEBUG("inventory", "inventory_add_proof(key=%s)\n", proof->header.key);

    if (inventory == NULL || proof == NULL) {
        errno = EINVAL;
        return -1;
    }

    // extend the pack array by one
    oldArray = inventory->proofs;
    newArray = malloc(sizeof(union store_proof) * (inventory->proofs_count + 1));
    if (!newArray) {
        return -1;
    }

    if (inventory->proofs_count) {
        memcpy(newArray, inventory->proofs, sizeof(union store_proof) * inventory->proofs_count);
    }

    entry = &((union store_proof*)newArray)[inventory->proofs_count];
    memcpy(entry, proof, sizeof(union store_proof));

    // Update the new array stored before we serialize the inventory to disk.
    inventory->proofs = newArray;
    inventory->proofs_count += 1;
    free(oldArray);
    return 0;
}

static json_t* __serialize_packs(struct store_inventory_pack* packs, int count)
{
    json_t* jspacks;
    VLOG_DEBUG("inventory", "__serialize_packs(count=%i)\n", count);

    jspacks = json_array();
    if (jspacks == NULL) {
        return NULL;
    }

    for (int i = 0; i < count; i++) {
        json_t* jspack = json_object();
        if (jspack == NULL) {
            return NULL;
        }

        json_object_set_new(jspack, "path", json_string(packs[i].path));
        json_object_set_new(jspack, "publisher", json_string(packs[i].publisher));
        json_object_set_new(jspack, "package", json_string(packs[i].package));
        json_object_set_new(jspack, "platform", json_string(packs[i].platform));
        json_object_set_new(jspack, "architecture", json_string(packs[i].arch));
        json_object_set_new(jspack, "channel", json_string(packs[i].channel));
        json_object_set_new(jspack, "revision", json_integer(packs[i].revision));

        json_array_append_new(jspacks, jspack);
    }
    return jspacks;
}

static int __serialize_publisher_proof(json_t* jsproof, struct __proof_publisher* proof)
{
    json_object_set_new(jsproof, "public-key", json_string(&proof->public_key[0]));
    json_object_set_new(jsproof, "signed-key", json_string(&proof->signed_key[0]));
    return 0;
}

static int __serialize_package_proof(json_t* jsproof, struct __proof_package* proof)
{
    json_object_set_new(jsproof, "signature", json_string(&proof->signature[0]));
    return 0;
}

static json_t* __serialize_proofs(union __proof* proofs, int count)
{
    json_t* jsproofs;
    VLOG_DEBUG("inventory", "__serialize_proofs(count=%i)\n", count);

    jsproofs = json_array();
    if (jsproofs == NULL) {
        return NULL;
    }

    for (int i = 0; i < count; i++) {
        json_t* jsproof = json_object();
        if (jsproof == NULL) {
            return NULL;
        }

        json_object_set_new(jsproof, "type", json_integer((long long)proofs[i].header.type));
        json_object_set_new(jsproof, "key", json_string(&proofs[i].header.key[0]));
        switch (proofs[i].header.type) {
            case STORE_PROOF_PUBLISHER:
                if (__serialize_publisher_proof(jsproof, &proofs[i].publisher)) {
                    VLOG_ERROR("inventory", "__parse_proofs: failed to parse publisher proof (index %i) from inventory\n", i);
                    return -1;
                };
                break;
            case STORE_PROOF_PACKAGE:
                if (__serialize_package_proof(jsproof, &proofs[i].publisher)) {
                    VLOG_ERROR("inventory", "__parse_proofs: failed to parse package proof (index %i) from inventory\n", i);
                    return -1;
                };
                break;
        }

        json_array_append_new(jsproofs, jsproof);
    }
    return jsproofs;
}

static int __serialize_inventory(struct store_inventory* inventory, json_t** jsonOut)
{
    json_t* root;
    json_t* packs;
    json_t* proofs;
    VLOG_DEBUG("inventory", "__serialize_inventory()\n");
    
    root = json_object();
    if (!root) {
        return -1;
    }

    packs = __serialize_packs(inventory->packs, inventory->packs_count);
    if (packs == NULL) {
        json_decref(root);
        return -1;
    }

    proofs = __serialize_proofs(inventory->proofs, inventory->proofs_count);
    if (proofs == NULL) {
        json_decref(packs);
        json_decref(root);
        return -1;
    }

    json_object_set_new(root, "packs", packs);
    json_object_set_new(root, "proofs", proofs);
    *jsonOut = root;
    return 0;
}

int inventory_save(struct store_inventory* inventory)
{
    json_t* root;
    int     status;
    char*   filePath;
    VLOG_DEBUG("inventory", "inventory_save()\n");

    if (inventory == NULL) {
        errno = EINVAL;
        return -1;
    }

    filePath = strpathcombine(inventory->path, "inventory.json");
    if (filePath == NULL) {
        return -1;
    }

    status = __serialize_inventory(inventory, &root);
    if (status) {
        free(filePath);
        return -1;
    }

    status = json_dump_file(root, filePath, JSON_INDENT(2));
    free(filePath);
    json_decref(root);
    return status;
}

void inventory_clear(struct store_inventory* inventory)
{
    VLOG_DEBUG("inventory", "inventory_clear()\n");
    for (int i = 0; i < inventory->packs_count; i++) {
        free((void*)inventory->packs[i].path);
        free((void*)inventory->packs[i].publisher);
        free((void*)inventory->packs[i].package);
        free((void*)inventory->packs[i].platform);
        free((void*)inventory->packs[i].arch);
        free((void*)inventory->packs[i].channel);
    }

    free(inventory->packs);
    inventory->packs_count = 0;
    inventory->packs = NULL;

    free(inventory->proofs);
    inventory->proofs_count = 0;
    inventory->proofs = NULL;
}

void inventory_free(struct store_inventory* inventory)
{
    if (inventory == NULL) {
        return;
    }
    inventory_clear(inventory);
    free((void*)inventory->path);
    free(inventory);
}

const char* inventory_pack_name(struct store_inventory_pack* pack)
{
    if (pack == NULL) {
        return NULL;
    }
    return pack->package;
}

const char* inventory_pack_path(struct store_inventory_pack* pack)
{
    if (pack == NULL) {
        return NULL;
    }
    return pack->path;
}

const char* inventory_pack_platform(struct store_inventory_pack* pack)
{
    if (pack == NULL) {
        return NULL;
    }
    return pack->platform;
}

const char* inventory_pack_arch(struct store_inventory_pack* pack)
{
    if (pack == NULL) {
        return NULL;
    }
    return pack->arch;
}
