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
#include <chef/fridge.h>
#include <jansson.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <vlog.h>

struct fridge_inventory_pack {
    // yay, parent pointers!
    void*       inventory;
    const char* path;
    const char* publisher;
    const char* package;
    const char* platform;
    const char* arch;
    const char* channel;
    int         revision;
};

struct fridge_inventory {
    const char*                   path;
    struct timespec               last_check;
    struct fridge_inventory_pack* packs;
    int                           packs_count;
    union fridge_proof*           proofs;
    int                           proofs_count;
};

static struct fridge_inventory* __inventory_new(void)
{
    struct fridge_inventory* inventory;

    inventory = (struct fridge_inventory*)malloc(sizeof(struct fridge_inventory));
    if (inventory == NULL) {
        return NULL;
    }

    memset(inventory, 0, sizeof(struct fridge_inventory));
    return inventory;
}

static struct timespec __parse_timespec(const char* timestamp)
{
    struct timespec ts;
    return ts;
}

static int __parse_inventory(const char* json, struct fridge_inventory** inventoryOut)
{
    struct fridge_inventory* inventory;
    json_error_t             error;
    json_t*                  root;
    json_t*                  last_check;
    json_t*                  packs;
    size_t                   pack_count;
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

    packs = json_object_get(root, "packs");
    pack_count = json_array_size(packs);
    VLOG_DEBUG("inventory", "__parse_inventory: number of packs %zu\n", pack_count);
    if (packs && pack_count > 0) {
        inventory->packs = (struct fridge_inventory_pack*)malloc(sizeof(struct fridge_inventory_pack) * pack_count);
        if (inventory->packs == NULL) {
            errno = ENOMEM;
            status = -1;
            goto exit;
        }

        memset(inventory->packs, 0, sizeof(struct fridge_inventory_pack) * pack_count);
        inventory->packs_count = pack_count;
        
        for (size_t i = 0; i < pack_count; i++) {
            json_t* pack = json_array_get(packs, i);
            json_t* path = json_object_get(pack, "path");
            json_t* publisher = json_object_get(pack, "publisher");
            json_t* name = json_object_get(pack, "package");
            json_t* platform = json_object_get(pack, "platform");
            json_t* architecture = json_object_get(pack, "architecture");
            json_t* channel = json_object_get(pack, "channel");
            json_t* revision = json_object_get(pack, "revision");

            inventory->packs[i].inventory = inventory;
            inventory->packs[i].path = platform_strdup(json_string_value(path));
            inventory->packs[i].publisher = platform_strdup(json_string_value(publisher));
            inventory->packs[i].package = platform_strdup(json_string_value(name));
            inventory->packs[i].platform = platform_strdup(json_string_value(platform));
            inventory->packs[i].arch = platform_strdup(json_string_value(architecture));
            inventory->packs[i].channel = platform_strdup(json_string_value(channel));
            inventory->packs[i].revision = json_integer_value(revision);
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

int inventory_load(const char* path, struct fridge_inventory** inventoryOut)
{
    struct fridge_inventory* inventory;
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

int inventory_get_pack(struct fridge_inventory* inventory, const char* publisher, 
    const char* package, const char* platform, const char* arch, const char* channel,
    int revision, struct fridge_inventory_pack** packOut)
{
    struct fridge_inventory_pack* result = NULL;
    VLOG_DEBUG("inventory", "inventory_get_pack()\n");

    if (inventory == NULL || publisher == NULL || package == NULL || channel == NULL) { 
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

int inventory_add(struct fridge_inventory* inventory, const char* packPath, const char* publisher,
    const char* package, const char* platform, const char* arch, const char* channel,
    int revision, struct fridge_inventory_pack** packOut)
{
    struct fridge_inventory_pack* packEntry;
    void*                         newArray;
    void*                         oldArray;

    if (inventory == NULL || publisher == NULL || 
        package == NULL   || channel == NULL) {
        errno = EINVAL;
        return -1;
    }

    // extend the pack array by one
    oldArray = inventory->packs;
    newArray = malloc(sizeof(struct fridge_inventory_pack) * (inventory->packs_count + 1));
    if (!newArray) {
        return -1;
    }

    if (inventory->packs_count) {
        memcpy(newArray, inventory->packs, sizeof(struct fridge_inventory_pack) * inventory->packs_count);
    }

    // now create the new entry
    packEntry = &((struct fridge_inventory_pack*)newArray)[inventory->packs_count];
    memset(packEntry, 0, sizeof(struct fridge_inventory_pack));

    packEntry->inventory = inventory;
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

int inventory_add_proof(struct fridge_inventory* inventory, union fridge_proof* proof)
{
    union fridge_proof* entry;
    void*               newArray;
    void*               oldArray;

    if (inventory == NULL || proof == NULL) {
        errno = EINVAL;
        return -1;
    }

    // extend the pack array by one
    oldArray = inventory->proofs;
    newArray = malloc(sizeof(union fridge_proof) * (inventory->proofs_count + 1));
    if (!newArray) {
        return -1;
    }

    if (inventory->proofs_count) {
        memcpy(newArray, inventory->proofs, sizeof(union fridge_proof) * inventory->proofs_count);
    }

    entry = &((union fridge_proof*)newArray)[inventory->proofs_count];
    memcpy(entry, proof, sizeof(union fridge_proof));

    // Update the new array stored before we serialize the inventory to disk.
    inventory->proofs = newArray;
    inventory->proofs_count += 1;
    free(oldArray);
    return 0;
}

static json_t* __serialize_packs(struct fridge_inventory_pack* packs, int count)
{
    json_t* packs;

    packs = json_array();
    if (!packs) {
        return NULL;
    }

    for (int i = 0; i < packs; i++) {
        json_t* pack = json_object();
        if (!pack) {
            return NULL;
        }

        json_object_set_new(pack, "path", json_string(packs[i].path));
        json_object_set_new(pack, "publisher", json_string(packs[i].publisher));
        json_object_set_new(pack, "package", json_string(packs[i].package));
        json_object_set_new(pack, "platform", json_string(packs[i].platform));
        json_object_set_new(pack, "architecture", json_string(packs[i].arch));
        json_object_set_new(pack, "channel", json_string(packs[i].channel));
        json_object_set_new(pack, "revision", json_integer(packs[i].revision));

        json_array_append_new(packs, pack);
    }
    return packs;
}

static int __serialize_inventory(struct fridge_inventory* inventory, json_t** jsonOut)
{
    json_t* root;
    json_t* packs;
    json_t* proofs;
    
    root = json_object();
    if (!root) {
        return -1;
    }

    packs = __serialize_packs(inventory->packs, inventory->packs_count);
    if (packs == NULL) {
        json_decref(root);
        return -1;
    }

    json_object_set_new(root, "packs", packs);
    *jsonOut = root;
    return 0;
}

int inventory_save(struct fridge_inventory* inventory)
{
    json_t* root;
    int     status;
    char*   filePath;

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

void inventory_clear(struct fridge_inventory* inventory)
{
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
}

void inventory_free(struct fridge_inventory* inventory)
{
    if (inventory == NULL) {
        return;
    }
    inventory_clear(inventory);
    free((void*)inventory->path);
    free(inventory);
}

const char* inventory_pack_name(struct fridge_inventory_pack* pack)
{
    if (pack == NULL) {
        return NULL;
    }
    return pack->package;
}

const char* inventory_pack_path(struct fridge_inventory_pack* pack)
{
    if (pack == NULL) {
        return NULL;
    }
    return pack->path;
}

const char* inventory_pack_platform(struct fridge_inventory_pack* pack)
{
    if (pack == NULL) {
        return NULL;
    }
    return pack->platform;
}

const char* inventory_pack_arch(struct fridge_inventory_pack* pack)
{
    if (pack == NULL) {
        return NULL;
    }
    return pack->arch;
}
