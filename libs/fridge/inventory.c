/**
 * Copyright 2022, Philip Meulengracht
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
#include <jansson.h>
#include <stdio.h>
#include <stdlib.h>

static struct fridge_inventory* __inventory_new(void)
{
    struct fridge_inventory* inventory;

    inventory = (struct fridge_inventory*)malloc(sizeof(struct fridge_inventory));
    if (inventory == NULL) {
        return -1;
    }
    memset(inventory, 0, sizeof(struct fridge_inventory));
    return inventory;
}

static struct timespec __parse_timespec(const char* timestamp)
{

}

static int __parse_inventory(const char* json, struct fridge_inventory** inventoryOut)
{
    struct fridge_inventory* inventory;
    json_error_t             error;
    json_t*                  root;
    json_t*                  last_check;
    json_t*                  packs;

    // instantiate a new inventory
    inventory = __inventory_new();
    if (json == NULL) {
        *inventoryOut = inventory;
        return 0;
    }

    printf("__parse_inventory: %s\n", json);

    root = json_loads(json, 0, &error);
    if (!root) {
        free(inventory);
        return -1;
    }

    // parse the root members
    last_check = json_object_get(root, "last_check");
    if (last_check) {
        inventory->last_check = __parse_timespec(json_string_value(last_check));
    }

    packs = json_object_get(root, "packs");
    if (packs) {
        size_t pack_count = json_array_size(packs);
        inventory->packs = (struct fridge_inventory_pack*)malloc(sizeof(struct fridge_inventory_pack) * pack_count);
        if (inventory->packs == NULL) {
            return -1;
        }

        memset(inventory->packs, 0, sizeof(struct fridge_inventory_pack) * pack_count);
        inventory->packs_count = pack_count;
        
        for (size_t i = 0; i < pack_count; i++) {
            json_t* pack = json_array_get(channels, i);
            json_t* publisher = json_object_get(channel, "version");
            json_t* name = json_object_get(version, "major");
            json_t* channel = json_object_get(version, "minor");
            json_t* version = json_object_get(pack, "version");
            json_t* latest = json_object_get(version, "latest");
            json_t* version_major = json_object_get(version, "major");
            json_t* version_minor = json_object_get(version, "minor");
            json_t* version_revision = json_object_get(version, "revision");
            json_t* version_tag = json_object_get(version, "tag");

            inventory->packs[i].publisher = strdup(json_string_value(publisher));
            inventory->packs[i].package = strdup(json_string_value(name));
            inventory->packs[i].channel = strdup(json_string_value(channel));
            inventory->packs[i].current_version.major = json_integer_value(version_major);
            inventory->packs[i].current_version.minor = json_integer_value(version_minor);
            inventory->packs[i].current_version.revision = json_integer_value(version_revision);
            inventory->packs[i].current_version.tag = json_string_value(version_tag);
            inventory->packs[i].latest = json_integer_value(latest);
        }
    }

    *inventoryOut = inventory;
    return 0;    
}

static int __inventory_load_file(const char* path, char** jsonOut)
{
    FILE*  file;
    size_t size;
    char*  json;

    file = fopen(path, "r+");
    if (file == NULL) {
        return -1;
    }

    fseek(file, SEEK_END, 0);
    size = ftell(file);
    fseek(file, SEEK_SET, 0);

    if (size) {
        json = (char*)malloc(size + 1); // sz?!
        if (!json) {
            fclose(file);
            return -1;
        }
        memset(json, 0, size + 1);
        fread(json, 1, size, file);
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

    if (path == NULL || inventoryOut == NULL) {
        errno = EINVAL;
        return -1;
    }

    status = __inventory_load_file(path, &json);
    if (status) {
        fprintf(stderr, "inventory_load: failed to load %s\n", path);
        return -1;
    }

    status = __parse_inventory(json, &inventory);
    free(json);

    if (status) {
        fprintf(stderr, "inventory_load: failed to parse the inventory, file corrupt??\n");
        return -1;
    }

    *inventoryOut = inventory;
    return 0;
}

int inventory_add(struct fridge_inventory* inventory, const char* publisher,
    const char* package, struct chef_version* version, int latest)
{
    void* newArray;
    void* oldArray;

    if (inventory == NULL || publisher == NULL || package == NULL) {
        errno = EINVAL;
        return -1;
    }

    // extend the pack array by one
    oldArray = inventory->packs;
    newArray = malloc(inventory->packs_count + 1);
    if (!newArray) {
        return -1;
    }

    if (inventory->packs_count) {
        memcpy(newArray, inventory->packs, sizeof(struct fridge_inventory_pack) * inventory->packs_count);
    }

    inventory->packs = newArray;
    inventory->packs_count += 1;
    free(oldArray);
    return 0;
}

int inventory_save(struct fridge_inventory* inventory, const char* path)
{
    if (inventory == NULL || path == NULL) {
        errno = EINVAL;
        return -1;
    }

    
}
