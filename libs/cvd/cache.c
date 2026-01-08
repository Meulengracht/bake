/**
 * Copyright 2024, Philip Meulengracht
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

#include <ctype.h>
#include <chef/platform.h>
#include <chef/recipe.h>
#include <errno.h>
#include <jansson.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <vlog.h>

struct build_cache_item {
    const char* name;
    const char* uuid;
    json_t*     keystore;
};

static int __construct_build_cache_item(struct build_cache_item* cacheItem, const char* name)
{
    VLOG_DEBUG("cache", "__construct_build_cache_item(name=%s)\n", name);

    memset(cacheItem, 0, sizeof(struct build_cache_item));
    
    cacheItem->name = platform_strdup(name);
    cacheItem->uuid = platform_secure_random_string_new(16);
    cacheItem->keystore = json_object();
    if (cacheItem->name == NULL || cacheItem->uuid == NULL || cacheItem->keystore == NULL) {
        free((void*)cacheItem->name);
        free((void*)cacheItem->uuid);
        json_decref(cacheItem->keystore);
        return -1;
    }
    return 0;
}

static int __parse_cache_item(struct build_cache_item* cacheItem, json_t* root)
{
    json_t* member;
    VLOG_DEBUG("cache", "__parse_cache_item()\n");

    member = json_object_get(root, "name");
    if (member == NULL) {
        return -1;
    }
    cacheItem->name = platform_strdup(json_string_value(member));

    member = json_object_get(root, "uuid");
    if (member == NULL) {
        return -1;
    }
    cacheItem->uuid = platform_strdup(json_string_value(member));

    cacheItem->keystore = json_object_get(root, "cache");
    if (cacheItem->keystore == NULL) {
        return -1;
    }

    return 0;
}

static json_t* __serialize_cache_item(struct build_cache_item* cacheItem)
{
    json_t* root;
    VLOG_DEBUG("cache", "__serialize_cache_item(cache=%s)\n", cacheItem->name);
    
    root = json_object();
    if (!root) {
        return NULL;
    }

    json_object_set_new(root, "name", json_string(cacheItem->name));
    json_object_set_new(root, "uuid", json_string(cacheItem->uuid));
    json_object_set(root, "cache", cacheItem->keystore);
    return root;
}

struct build_cache {
    struct recipe*            current;
    const char*               path;

    struct build_cache_item* items;
    int                       item_count;

    int                       xaction;
};

static struct build_cache* __build_cache_new(const char* path, struct recipe* recipe)
{
    struct build_cache* cache = calloc(1, sizeof(struct build_cache));
    if (cache == NULL) {
        return NULL;
    }

    if (path != NULL) {
        cache->path = platform_strdup(path);
        if (cache->path == NULL) {
            free(cache);
            return NULL;
        }
    }

    cache->current = recipe;
    return cache;
}

static void __build_cache_delete(struct build_cache* cache)
{
    if (cache == NULL) {
        return;
    }

    free((void*)cache->path);
    free(cache);
}

static int __parse_cache(struct build_cache* cache, json_t* root)
{
    json_t* cacheItems;
    size_t  length;

    cacheItems = json_object_get(root, "caches");
    if (cacheItems == NULL) {
        return 0;
    }

    length = json_array_size(cacheItems);
    if (length == 0) {
        return 0;
    }

    cache->item_count = (int)length;
    cache->items = calloc(length, sizeof(struct build_cache_item));
    if (cache->items == NULL) {
        return -1;
    }

    for (size_t i = 0; i < length; i++) {
        json_t* cacheItem = json_array_get(cacheItems, i);
        if (__parse_cache_item(&cache->items[i], cacheItem)) {
            return -1;
        }
    }
    return 0;
}

static int __load_config(struct build_cache* cache, const char* path)
{
    json_error_t error;
    json_t*      root;

    root = json_load_file(path, 0, &error);
    if (root == NULL) {
        if (json_error_code(&error) == json_error_cannot_open_file) {
            // assume no cache
            return 0;
        }
        return -1;
    }
    return __parse_cache(cache, root);
}

static json_t* __serialize_cache(struct build_cache* cache)
{
    json_t* root;
    json_t* items;
    VLOG_DEBUG("cache", "__serialize_cache(cache=%s)\n", cache->path);
    
    root = json_object();
    if (!root) {
        VLOG_ERROR("cache", "__serialize_cache: failed to allocate memory for root object\n");
        return NULL;
    }
    
    items = json_array();
    if (root == NULL) {
        VLOG_ERROR("cache", "__serialize_cache: failed to allocate memory for cache array\n");
        json_decref(root);
        return NULL;
    }

    for (int i = 0; i < cache->item_count; i++) {
        json_t* item = __serialize_cache_item(&cache->items[i]);
        if (item == NULL) {
            VLOG_ERROR("cache", "__serialize_cache: failed to serialize cache for %s\n", cache->items[i].name);
            return NULL;
        }
        json_array_append_new(items, item);
    }

    json_object_set_new(root, "caches", items);
    return root;
}

static int __save_cache(struct build_cache* cache)
{
    json_t* root;
    VLOG_DEBUG("cache", "__save_cache(cache=%s)\n", cache->path);

    // ignore NULL caches
    if (cache->path == NULL) {
        return 0;
    }

    root = __serialize_cache(cache);
    if (root == NULL) {
        VLOG_ERROR("cache", "__save_cache: failed to serialize cache\n");
        return -1;
    }
    
    if (json_dump_file(root, cache->path, JSON_INDENT(2)) < 0) {
        VLOG_ERROR("cache", "__save_cache: failed to write cache to file\n");
        return -1;
    }
    return 0;
}

static int __ensure_build_cache(struct build_cache* cache, struct recipe* current)
{
    void* expanded;

    if (current == NULL) {
        return 0;
    }

    for (int i = 0; i < cache->item_count; i++) {
        if (strcmp(cache->items[i].name, current->project.name) == 0) {
            // exists
            return 0;
        }
    }

    expanded = realloc(cache->items, sizeof(struct build_cache_item) * (cache->item_count + 1));
    if (expanded == NULL) {
        return -1;
    }
    if (__construct_build_cache_item(&(((struct build_cache_item*)expanded)[cache->item_count]), current->project.name)) {
        return -1;
    }

    cache->items = expanded;
    cache->item_count++;
    return 0;
}

// API
int build_cache_create(struct recipe* current, const char* cwd, struct build_cache** cacheOut)
{
    struct build_cache* cache;
    char                 buff[PATH_MAX] = { 0 };
    int                  status;

    snprintf(&buff[0], sizeof(buff), "%s" CHEF_PATH_SEPARATOR_S ".vchcache", cwd);

    // initialize the random counter for guid's
    srand(clock());

    // create a new cache
    cache = __build_cache_new(&buff[0], current);
    if (cache == NULL) {
        VLOG_ERROR("cache", "out of memory for cache allocation!\n");
        return -1;
    }

    status = __load_config(cache, &buff[0]);
    if (status) {
        VLOG_ERROR("cache", "failed to load or initialize the recipe cache\n");
        __build_cache_delete(cache);
        return status;
    }

    status = __ensure_build_cache(cache, current);
    if (status) {
        VLOG_ERROR("cache", "failed to ensure cache for current recipe\n");
        __build_cache_delete(cache);
        return status;
    }
    *cacheOut = cache;
    return 0;
}

int build_cache_create_null(struct recipe* current, struct build_cache** cacheOut)
{
    struct build_cache* cache;

    cache = __build_cache_new(NULL, current);
    if (cache == NULL) {
        VLOG_ERROR("cache", "out of memory for cache allocation!\n");
        return -1;
    }

    *cacheOut = cache;
    return 0;
}

const char* build_cache_uuid_for(struct build_cache* cache, const char* name)
{
    for (int i = 0; i < cache->item_count; i++) {
        if (strcmp(cache->items[i].name, name) == 0) {
            return cache->items[i].uuid;
        }
    }

    VLOG_FATAL("cache", "no cache entry for %s\n", name);
    return NULL;
}

const char* build_cache_uuid(struct build_cache* cache)
{
    if (cache->current != NULL) {
        return build_cache_uuid_for(cache, cache->current->project.name);
    }

    VLOG_FATAL("cache", "no recipe specified\n");
    return NULL;
}

static struct build_cache_item* __get_cache_item(struct build_cache* cache)
{
    if (cache->current == NULL) {
        VLOG_FATAL("cache", "__get_cache_item: invoked but no recipe set\n");
        return NULL;
    }

    // ignore NULL caches
    if (cache->path == NULL) {
        return NULL;
    }

    for (int i = 0; i < cache->item_count; i++) {
        if (strcmp(cache->items[i].name, cache->current->project.name) == 0) {
            return &cache->items[i];
        }
    }

    VLOG_FATAL("cache", "no cache entry for %s\n", cache->current->project.name);
    return NULL;
}

int build_cache_clear_for(struct build_cache* cache, const char* name)
{
    if (!cache->xaction) {
        VLOG_FATAL("cache", "build_cache_clear_for: no transaction\n");
    }

    for (int i = 0; i < cache->item_count; i++) {
        if (strcmp(cache->items[i].name, name) == 0) {
            json_object_clear(cache->items[i].keystore);
            return 0;
        }
    }
    errno = ENOENT;
    return -1;
}

void build_cache_transaction_begin(struct build_cache* cache)
{
    if (cache->xaction != 0) {
        VLOG_FATAL("cache", "transaction already in progress\n");
    }
    cache->xaction = 1;
}

void build_cache_transaction_commit(struct build_cache* cache)
{
    if (!cache->xaction) {
        VLOG_FATAL("cache", "no transaction in progress\n");
    }

    if (__save_cache(cache)) {
        VLOG_FATAL("cache", "failed to commit changes to cache\n");
    }
    cache->xaction = 0;
}

const char* build_cache_key_string(struct build_cache* cache, const char* key)
{
    struct build_cache_item* cacheItem = __get_cache_item(cache);
    if (cacheItem == NULL) {
        return NULL;
    }
    return json_string_value(json_object_get(cacheItem->keystore, key));
}

int build_cache_key_set_string(struct build_cache* cache, const char* key, const char* value)
{
    struct build_cache_item* cacheItem = __get_cache_item(cache);
    int                       status;

    if (!cache->xaction) {
        VLOG_FATAL("cache", "build_cache_key_set_string: no transaction\n");
    }

    if (cacheItem == NULL) {
        return 0;
    }
    
    status = json_object_set_new(cacheItem->keystore, key, json_string(value));
    if (status) {
        VLOG_ERROR("cache", "failed to update value %s for %s: %i\n", key, value, status);
    }
    return status;
}

int build_cache_key_bool(struct build_cache* cache, const char* key)
{
    const char* value = build_cache_key_string(cache, key);
    if (value == NULL) {
        return 0;
    }
    return strcmp(value, "true") == 0 ? 1 : 0;
}

int build_cache_key_set_bool(struct build_cache* cache, const char* key, int value)
{
    return build_cache_key_set_string(cache, key, value ? "true" : "false");
}

int build_cache_is_part_sourced(struct build_cache* cache, const char* part)
{
    char buffer[256];
    snprintf(&buffer[0], sizeof(buffer), "%s-sourced", part);
    return build_cache_key_bool(cache, &buffer[0]);
}

int build_cache_mark_part_sourced(struct build_cache* cache, const char* part)
{
    char buffer[256];
    snprintf(&buffer[0], sizeof(buffer), "%s-sourced", part);
    return build_cache_key_set_bool(cache, &buffer[0], 1);
}

int build_cache_mark_step_complete(struct build_cache* cache, const char* part, const char* step)
{
    char buffer[256];
    snprintf(&buffer[0], sizeof(buffer), "%s-%s", part, step);
    return build_cache_key_set_bool(cache, &buffer[0], 1);
}

int build_cache_mark_step_incomplete(struct build_cache* cache, const char* part, const char* step)
{
    char buffer[256];
    snprintf(&buffer[0], sizeof(buffer), "%s-%s", part, step);
    return build_cache_key_set_bool(cache, &buffer[0], 0);
}

int build_cache_is_step_complete(struct build_cache* cache, const char* part, const char* step)
{
    char buffer[256];
    snprintf(&buffer[0], sizeof(buffer), "%s-%s", part, step);
    return build_cache_key_bool(cache, &buffer[0]);
}
