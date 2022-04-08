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

#include <chef/client.h>
#include <errno.h>
#include "inventory.h"
#include <jansson.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

struct fridge_inventory_pack {
    // yay, parent pointers!
    void*               inventory;
    const char*         publisher;
    const char*         package;
    const char*         platform;
    const char*         arch;
    const char*         channel;
    struct chef_version version;
    int                 latest;
    int                 unpacked;
};

struct fridge_inventory {
    const char*                   path;
    struct timespec               last_check;
    struct fridge_inventory_pack* packs;
    int                           packs_count;
};

#define PACKAGE_TEMP_PATH "pack.inprogress"

static char* __combine(const char* a, const char* b)
{
    char* combined;
    int   status;

    combined = malloc(strlen(a) + strlen(b) + 2);
    if (combined == NULL) {
        return NULL;
    }

    status = sprintf(combined, "%s/%s", a, b);
    if (status < 0) {
        free(combined);
        return NULL;
    }

    return combined;
}

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

}

static int __parse_inventory(const char* json, struct fridge_inventory** inventoryOut)
{
    struct fridge_inventory* inventory;
    json_error_t             error;
    json_t*                  root;
    json_t*                  last_check;
    json_t*                  packs;
    int                      status;

    // instantiate a new inventory
    inventory = __inventory_new();
    if (json == NULL) {
        *inventoryOut = inventory;
        return 0;
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
    if (packs) {
        size_t pack_count = json_array_size(packs);
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
            json_t* publisher = json_object_get(pack, "publisher");
            json_t* name = json_object_get(pack, "package");
            json_t* platform = json_object_get(pack, "platform");
            json_t* architecture = json_object_get(pack, "architecture");
            json_t* channel = json_object_get(pack, "channel");
            json_t* latest = json_object_get(pack, "latest");
            json_t* unpacked = json_object_get(pack, "unpacked");

            json_t* version = json_object_get(pack, "version");
            json_t* version_major = json_object_get(version, "major");
            json_t* version_minor = json_object_get(version, "minor");
            json_t* version_patch = json_object_get(version, "patch");
            json_t* version_revision = json_object_get(version, "revision");
            json_t* version_tag = json_object_get(version, "tag");

            inventory->packs[i].publisher = strdup(json_string_value(publisher));
            inventory->packs[i].package = strdup(json_string_value(name));
            inventory->packs[i].platform = strdup(json_string_value(platform));
            inventory->packs[i].arch = strdup(json_string_value(architecture));
            inventory->packs[i].channel = strdup(json_string_value(channel));
            inventory->packs[i].version.major = json_integer_value(version_major);
            inventory->packs[i].version.minor = json_integer_value(version_minor);
            inventory->packs[i].version.patch = json_integer_value(version_patch);
            inventory->packs[i].version.revision = json_integer_value(version_revision);
            inventory->packs[i].version.tag = json_string_value(version_tag);
            inventory->packs[i].latest = json_integer_value(latest);
            inventory->packs[i].unpacked = json_integer_value(unpacked);
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
            fprintf(stderr, "__inventory_load_file: failed to read file: %s\n", strerror(errno));
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
    
    if (path == NULL || inventoryOut == NULL) {
        errno = EINVAL;
        return -1;
    }

    filePath = __combine(path, "inventory.json");
    if (filePath == NULL) {
        return -1;
    }

    status = __inventory_load_file(filePath, &json);
    if (status) {
        free(filePath);
        fprintf(stderr, "inventory_load: failed to load %s\n", filePath);
        return -1;
    }
    free(filePath);

    status = __parse_inventory(json, &inventory);
    free(json);

    if (status) {
        fprintf(stderr, "inventory_load: failed to parse the inventory, file corrupt??\n");
        return -1;
    }

    // store the base path of the inventory
    inventory->path = strdup(path);
    *inventoryOut = inventory;
    return 0;
}

int __compare_version(struct chef_version* version1, struct chef_version* version2)
{
    if (version1->revision != 0 && version2->revision != 0) {
        if (version1->revision > version2->revision) {
            return 1;
        } else if (version1->revision < version2->revision) {
            return -1;
        }
        return 0;
    }

    if (version1->major == version2->major && 
        version1->minor == version2->minor &&
        version1->patch == version2->patch) {
        return 0;
    }
    return -1;
}

int inventory_get_pack(struct fridge_inventory* inventory, const char* publisher, 
    const char* package, const char* platform, const char* arch, const char* channel,
    struct chef_version* version, struct fridge_inventory_pack** packOut)
{
    if (inventory == NULL || publisher == NULL || package == NULL || channel == NULL) {
        errno = EINVAL;
        return -1;
    }

    for (int i = 0; i < inventory->packs_count; i++) {
        if (strcmp(inventory->packs[i].publisher, publisher) == 0 &&
            strcmp(inventory->packs[i].package, package) == 0 &&
            strcmp(inventory->packs[i].platform, platform) == 0 &&
            strcmp(inventory->packs[i].arch, arch) == 0 &&
            strcmp(inventory->packs[i].channel, channel) == 0) {
            // ok same package, check version against latest
            if (inventory->packs[i].latest == 1 && version == NULL) {
                *packOut = &inventory->packs[i];
                return 0;
            } else if (inventory->packs[i].latest == 0 && version != NULL) {
                if (__compare_version(&inventory->packs[i].version, version) == 0) {
                    *packOut = &inventory->packs[i];
                    return 0;
                }
            }
        }
    }

    errno = ENOENT;
    return -1;
}

static int __package_path(struct fridge_inventory* inventory, const char* publisher,
    const char* package, const char* platform, const char* arch, const char* channel,
    int revision, char* pathBuffer, size_t bufferSize)
{
    int written;

    if (revision == 0) {
        fprintf(stderr, "__package_path: revision is not provided, but is required.\n");
        errno = EINVAL;
        return -1;
    }

    written = snprintf(
        pathBuffer, bufferSize - 1,
        "%s/%s-%s-%s-%s-%s-%i.pack",
        inventory->path,
        publisher, package,
        platform, arch,
        channel, revision
    );
    if (written == bufferSize - 1) {
        errno = ERANGE;
        return -1;
    }
    if (written < 0) {
        return -1;
    }
    return 0;
}

static int __inventory_download(struct fridge_inventory* inventory, const char* publisher,
    const char* package, const char* platform, const char* arch, const char* channel,
    struct chef_version* version)
{
    struct chef_download_params downloadParams;
    int                         status;
    char                        pathBuffer[512];

    // initialize download params
    downloadParams.publisher = publisher;
    downloadParams.package   = package;
    downloadParams.platform  = platform;
    downloadParams.arch      = arch;
    downloadParams.channel   = channel;
    downloadParams.version   = version;

    status = chefclient_pack_download(&downloadParams, PACKAGE_TEMP_PATH);
    if (status) {
        fprintf(stderr, "__inventory_download: failed to download %s/%s\n", publisher, package);
        return -1;
    }

    // move the package into the right place
    status = __package_path(inventory, publisher, package, platform, arch, channel, downloadParams.revision, &pathBuffer[0], sizeof(pathBuffer));
    if (status) {
        fprintf(stderr, "__inventory_download: package path too long!\n");
        return -1;
    }
    status = rename(PACKAGE_TEMP_PATH, &pathBuffer[0]);

    return status;
}

int inventory_add(struct fridge_inventory* inventory, const char* publisher,
    const char* package, const char* platform, const char* arch, const char* channel,
    struct chef_version* version, struct fridge_inventory_pack** packOut)
{
    struct fridge_inventory_pack* packEntry;
    void*                         newArray;
    void*                         oldArray;
    int                           status;

    if (inventory == NULL || publisher == NULL || 
        package == NULL   || channel == NULL) {
        errno = EINVAL;
        return -1;
    }

    status = __inventory_download(inventory, publisher, package, platform, arch, channel, version);
    if (status) {
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
    packEntry->publisher = strdup(publisher);
    packEntry->package   = strdup(package);
    packEntry->platform  = platform != NULL ? strdup(platform) : NULL;
    packEntry->arch      = platform != NULL ? strdup(arch) : NULL;
    packEntry->channel   = strdup(channel);
    if (version == NULL) {
        packEntry->latest = 1;
    } else {
        packEntry->version.major = version->major;
        packEntry->version.minor = version->minor;
        packEntry->version.patch = version->patch;
        packEntry->version.revision = version->revision;
        if (version->tag) {
            packEntry->version.tag = strdup(version->tag);
        }
    }

    *packOut = packEntry;

    inventory->packs = newArray;
    inventory->packs_count += 1;
    free(oldArray);
    return 0;
}

static int __serialize_inventory(struct fridge_inventory* inventory, json_t** jsonOut)
{
    json_t* root;
    json_t* packs;
    
    root = json_object();
    if (!root) {
        return -1;
    }

    packs = json_array();
    if (!packs) {
        json_decref(root);
        return -1;
    }

    for (int i = 0; i < inventory->packs_count; i++) {
        json_t* pack = json_object();
        if (!pack) {
            json_decref(root);
            return -1;
        }

        json_object_set_new(pack, "publisher", json_string(inventory->packs[i].publisher));
        json_object_set_new(pack, "package", json_string(inventory->packs[i].package));
        json_object_set_new(pack, "platform", json_string(inventory->packs[i].platform));
        json_object_set_new(pack, "architecture", json_string(inventory->packs[i].arch));
        json_object_set_new(pack, "channel", json_string(inventory->packs[i].channel));
        json_object_set_new(pack, "latest", json_integer(inventory->packs[i].latest));
        json_object_set_new(pack, "unpacked", json_integer(inventory->packs[i].unpacked));

        json_t* version = json_object();
        if (!version) {
            json_decref(root);
            return -1;
        }

        json_object_set_new(version, "major", json_integer(inventory->packs[i].version.major));
        json_object_set_new(version, "minor", json_integer(inventory->packs[i].version.minor));
        json_object_set_new(version, "patch", json_integer(inventory->packs[i].version.patch));
        json_object_set_new(version, "revision", json_integer(inventory->packs[i].version.revision));
        json_object_set_new(version, "tag", json_string(inventory->packs[i].version.tag));

        json_object_set_new(pack, "version", version);
        json_array_append_new(packs, pack);
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

    filePath = __combine(inventory->path, "inventory.json");
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

void inventory_free(struct fridge_inventory* inventory)
{
    if (inventory == NULL) {
        return;
    }

    for (int i = 0; i < inventory->packs_count; i++) {
        free((void*)inventory->packs[i].publisher);
        free((void*)inventory->packs[i].package);
        free((void*)inventory->packs[i].platform);
        free((void*)inventory->packs[i].arch);
        free((void*)inventory->packs[i].channel);
        free((void*)inventory->packs[i].version.tag);
    }

    free(inventory->packs);
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

int inventory_pack_filename(struct fridge_inventory_pack* pack, char* buffer, size_t size)
{
    int written;

    if (pack == NULL || buffer == NULL) {
        errno = EINVAL;
        return -1;
    }

    return __package_path(
        pack->inventory,
        pack->publisher,
        pack->package,
        pack->platform,
        pack->arch,
        pack->channel,
        pack->version.revision,
        buffer,
        size
    );
}

void inventory_pack_set_unpacked(struct fridge_inventory_pack* pack)
{
    if (pack == NULL) {
        return;
    }

    pack->unpacked = 1;
}

int inventory_pack_is_unpacked(struct fridge_inventory_pack* pack)
{
    if (pack == NULL) {
        errno = EINVAL;
        return -1;
    }
    return pack->unpacked;
}
