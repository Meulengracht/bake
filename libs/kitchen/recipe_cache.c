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
#include <jansson.h>
#include <stdlib.h>
#include <string.h>
#include <vlog.h>

struct recipe_cache_package {
    const char* name;
};

struct recipe_cache_ingredient {
    const char* name;
};

struct recipe_cache_item {
    const char* name;
    const char* uuid;
    struct list packages;    // list<recipe_cache_package>
    struct list ingredients; // list<recipe_cache_ingredient>
    json_t*     keystore;
};

struct recipe_cache {
    struct recipe*            current;
    const char*               path;
    struct recipe_cache_item* items;
    int                       item_count;
};

static const char*         g_uuidFmt = "xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx";
static const char*         g_hex = "0123456789ABCDEF-";
static struct recipe_cache g_cache = { 0 };

static void __generate_cache_uuid(char uuid[40])
{
    int length = strlen(g_uuidFmt);
    for (int i = 0; i < (length + 1); i++) {
        int r = rand() % 16;
        char c = ' ';   
        
        switch (g_uuidFmt[i]) {
            case 'x' : { c = g_hex[r]; } break;
            case 'y' : { c = g_hex[r & 0x03 | 0x08]; } break;
            case '-' : { c = '-'; } break;
            case '4' : { c = '4'; } break;
        }
        uuid[i] = (i < length) ? c : 0x00;
    }
}

static int __parse_cache(struct recipe_cache* cache, json_t* root)
{
    
}

static int __initialize_cache(struct recipe_cache* cache)
{

}

static int __load_cache(struct recipe_cache* cache)
{
    json_error_t error;
    json_t*      root;

    root = json_load_file("", 0, &error);
    if (root == NULL) {
        if (json_error_code(&error) == json_error_cannot_open_file) {
            return __initialize_cache(cache);
        }
        return -1;
    }
    return __parse_cache(cache, root);
}

static json_t* __serialize_cache_item(struct recipe_cache_item* cacheItem)
{
    json_t* root;
    
    root = json_object();
    if (!root) {
        return NULL;
    }
    
    json_object_set_new(root, "name", json_string(cacheItem->name));
    json_object_set_new(root, "uuid", json_string(cacheItem->uuid));
    json_object_set_new(root, "cache", cacheItem->keystore);
    return root;
}

static json_t* __serialize_cache(struct recipe_cache* cache)
{
    json_t* root;
    json_t* items;
    
    root = json_object();
    if (!root) {
        return NULL;
    }
    
    items = json_array();
    if (root == NULL) {
        json_decref(root);
        return NULL;
    }

    for (int i = 0; i < cache->item_count; i++) {
        json_t* item = __serialize_cache_item(&cache->items[i]);
        if (item == NULL) {
            return NULL;
        }
        json_array_append_new(root, item);
    }

    json_object_set_new(root, "caches", items);
    return root;
}

static int __save_cache(struct recipe_cache* cache)
{
    json_t* root;

    root = __serialize_cache(cache);
    if (root == NULL) {
        return -1;
    }
    return json_dump_file(root, cache->path, JSON_INDENT(2));
}

int recipe_cache_initialize(struct recipe* current)
{
    int status;

    // initialize the random counter for guid's
    srand(clock());

    status = __load_cache(&g_cache);
    if (status) {
        VLOG_ERROR("cache", "failed to load or initialize the recipe cache\n");
        return status;
    }

    return 0;
}

const char* recipe_cache_uuid_for(const char* name)
{
    for (int i = 0; i < g_cache.item_count; i++) {
        if (strcmp(g_cache.items[i].name, name) == 0) {
            return g_cache.items[i].uuid;
        }
    }

    VLOG_FATAL("cache", "no cache entry for %s\n", name);
    return NULL;
}

const char* recipe_cache_uuid(void)
{
    if (g_cache.current != NULL) {
        return recipe_cache_uuid_for(g_cache.current->project.name);
    }

    VLOG_FATAL("cache", "no recipe specified\n");
    return NULL;
}

static struct recipe_cache_item* __get_cache_item(void)
{
    for (int i = 0; i < g_cache.item_count; i++) {
        if (strcmp(g_cache.items[i].name, g_cache.current->project.name) == 0) {
            return &g_cache.items[i];
        }
    }

    VLOG_FATAL("cache", "no cache entry for %s\n", g_cache.current->project.name);
    return NULL;
}

static void __clear_packages(struct list* packages)
{

}

static void __clear_ingredients(struct list* ingredients)
{

}

int recipe_cache_clear_for(const char* name)
{
    for (int i = 0; i < g_cache.item_count; i++) {
        if (strcmp(g_cache.items[i].name, name) == 0) {
            __clear_packages(&g_cache.items[i].packages);
            __clear_ingredients(&g_cache.items[i].ingredients);
            json_object_clear(g_cache.items[i].keystore);
            return 0;
        }
    }
    return -1;
}

const char* recipe_cache_key_string(const char* key)
{
    struct recipe_cache_item* cache = __get_cache_item();
    json_t*                   value;

    value = json_object_get(cache->keystore, key);
    return json_string_value(value);
}

int recipe_cache_key_set_string(const char* key, const char* value)
{
    struct recipe_cache_item* cache = __get_cache_item();
    json_t*                   obj   = json_string(value);
    int                       status;

    status = json_object_set(cache->keystore, key, obj);
    json_decref(obj);
    return status;
}

int recipe_cache_key_bool(const char* key)
{
    struct recipe_cache_item* cache = __get_cache_item();
    const char*               value = recipe_cache_key_string(key);
    if (value == NULL) {
        return 0;
    }
    return strcmp(value, "true") == 0 ? 1 : 0;
}

int recipe_cache_key_set_bool(const char* key, int value)
{
    struct recipe_cache_item* cache = __get_cache_item();
    return recipe_cache_key_set_string(key, value ? "true" : "false");
}

int recipe_cache_mark_step_complete(const char* part, const char* step)
{
    struct recipe_cache_item* cache = __get_cache_item();
    char                      buffer[256];

    snprintf(&buffer[0], sizeof(buffer), "%s-%s", part, step);
    return recipe_cache_key_set_bool(&buffer[0], 1);
}

int recipe_cache_mark_step_incomplete(const char* part, const char* step)
{
    struct recipe_cache_item* cache = __get_cache_item();
    char                      buffer[256];

    snprintf(&buffer[0], sizeof(buffer), "%s-%s", part, step);
    return recipe_cache_key_set_bool(&buffer[0], 0);
}

int recipe_cache_is_step_complete(const char* part, const char* step)
{
    struct recipe_cache_item* cache = __get_cache_item();
    char                      buffer[256];

    snprintf(&buffer[0], sizeof(buffer), "%s-%s", part, step);
    return recipe_cache_key_bool(&buffer[0]);
}

int recipe_cache_calculate_package_changes(struct recipe_cache_package_change** changes, int* changeCount)
{
    struct recipe_cache_item* cache = __get_cache_item();


    *changes = NULL;
    *changeCount = 0;
    return 0;
}

int recipe_cache_commit_package_changes(struct recipe_cache_package_change* changes, int count)
{
    struct recipe_cache_item* cache = __get_cache_item();
    int                       status;


    status = __save_cache(&g_cache);
    if (status) {
        VLOG_FATAL("cache", "failed to save cache\n");
        return status;
    }
    return 0;
}
