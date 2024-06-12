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
#include <vlog.h>

struct recipe_cache_package {
    struct list_item list_header;
    const char*      name;
};

struct recipe_cache_ingredient {
    struct list_item list_header;
    const char*      name;
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

static json_t* __serialize_recipe_cache_package(struct recipe_cache_package* pkg)
{
    json_t* root;
    
    root = json_object();
    if (!root) {
        return NULL;
    }

    json_object_set_new(root, "name", json_string(pkg->name));
    return root;
}

static json_t* __serialize_cache_item_packages(struct list* packages)
{
    struct list_item* i;
    json_t*           root;

    root = json_array();
    if (!root) {
        return NULL;
    }

    list_foreach(packages, i) {
        json_t* pkg = __serialize_recipe_cache_package((struct recipe_cache_package*)i);
        if (pkg == NULL) {
            json_decref(root);
            return NULL;
        }
        json_array_append_new(root, pkg);
    }
    return root;
}

static json_t* __serialize_recipe_cache_ingredient(struct recipe_cache_ingredient* pkg)
{
    json_t* root;
    
    root = json_object();
    if (!root) {
        return NULL;
    }

    json_object_set_new(root, "name", json_string(pkg->name));
    return root;
}

static json_t* __serialize_cache_item_ingredients(struct list* ingredients)
{
    struct list_item* i;
    json_t*           root;

    root = json_array();
    if (!root) {
        return NULL;
    }

    list_foreach(ingredients, i) {
        json_t* ing = __serialize_recipe_cache_ingredient((struct recipe_cache_ingredient*)i);
        if (ing == NULL) {
            json_decref(root);
            return NULL;
        }
        json_array_append_new(root, ing);
    }
    return root;
}

static json_t* __serialize_cache_item(struct recipe_cache_item* cacheItem)
{
    json_t* root;
    json_t* packages;
    json_t* ingredients;
    
    root = json_object();
    if (!root) {
        return NULL;
    }

    packages = __serialize_cache_item_packages(&cacheItem->packages);
    if (packages == NULL) {
        json_decref(root);
        return NULL;
    }

    ingredients = __serialize_cache_item_ingredients(&cacheItem->ingredients);
    if (packages == NULL) {
        json_decref(packages);
        json_decref(root);
        return NULL;
    }
    
    json_object_set_new(root, "name", json_string(cacheItem->name));
    json_object_set_new(root, "uuid", json_string(cacheItem->uuid));
    json_object_set_new(root, "packages", packages);
    json_object_set_new(root, "ingredients", ingredients);
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
    struct list_item* i;
    while ((i = packages->head)) {
        struct recipe_cache_package* pkg = (struct recipe_cache_package*)i;
        packages->head = i->next;

        free((char*)pkg->name);
        free(pkg);
    }
}

static void __clear_ingredients(struct list* ingredients)
{
    struct list_item* i;
    while ((i = ingredients->head)) {
        struct recipe_cache_ingredient* ing = (struct recipe_cache_ingredient*)i;
        ingredients->head = i->next;

        free((char*)ing->name);
        free(ing);
    }
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

static int __add_package_change(
    struct recipe_cache_package_change** changes,
    int*                                 changeCount,
    int*                                 capacity,
    const char*                          name,
    enum recipe_cache_change_type        changeType)
{
    int index = *changeCount;

    if (*changes == NULL) {
        *changes = calloc(8, sizeof(struct recipe_cache_package_change));
        if (*changes == NULL) {
            return -1;
        }
        *capacity = 8;
    }

    if (index == *capacity) {
        *changes = realloc(*changes, (*capacity) * 2);
        if (*changes == NULL) {
            return -1;
        }
        *capacity *= 2;
        memset(&(*changes)[index], 0, sizeof(struct recipe_cache_package_change) * ((*capacity) - index));
    }

    (*changes)[index].name = name;
    (*changes)[index].type = changeType;
    (*changeCount)++;
    return 0;
}

int recipe_cache_calculate_package_changes(struct recipe_cache_package_change** changes, int* changeCount)
{
    struct recipe_cache_item* cache = __get_cache_item();
    struct list_item          *i, *j;
    int                       capacity = 0;

    *changes = NULL;
    *changeCount = 0;

    // We use an insanely inefficient algorithm here, but we don't care as
    // these lists should never be long, and we do not have access to an easy
    // hashtable here. 

    // check packages added
    list_foreach(&g_cache.current->environment.host.packages, i) {
        struct oven_value_item* toCheck = (struct oven_value_item*)i;
        int                     exists = 0;
        list_foreach(&cache->packages, j) {
            struct recipe_cache_package* pkg = (struct recipe_cache_package*)i;
            if (strcmp(toCheck->value, pkg->name) == 0) {
                // found, not a new package
                exists = 1;
                break;
            }
        }
        if (!exists) {
            if(__add_package_change(changes, changeCount, &capacity, toCheck->value, RECIPE_CACHE_CHANGE_ADDED)) {
                return -1;
            }
        }
    }

    // check packages removed
    list_foreach(&cache->packages, i) {
        struct oven_value_item* toCheck = (struct oven_value_item*)i;
        int                     exists = 0;
        list_foreach(&g_cache.current->environment.host.packages, j) {
            struct recipe_cache_package* pkg = (struct recipe_cache_package*)i;
            if (strcmp(toCheck->value, pkg->name) == 0) {
                // found, not a new package
                exists = 1;
                break;
            }
        }
        if (!exists) {
            if (__add_package_change(changes, changeCount, &capacity, toCheck->value, RECIPE_CACHE_CHANGE_REMOVED)) {
                return -1;
            }
        }
    }
    return 0;
}

static struct recipe_cache_package* __new_recipe_cache_package(const char* name)
{

}

int recipe_cache_commit_package_changes(struct recipe_cache_package_change* changes, int count)
{
    struct recipe_cache_item* cache = __get_cache_item();
    int                       status;

    if (changes == NULL || count == 0) {
        errno = EINVAL;
        return -1;
    }

    for (int i = 0; i < count; i++) {
        switch (changes[i].type) {
            case RECIPE_CACHE_CHANGE_ADDED: {
                struct recipe_cache_package* pkg = __new_recipe_cache_package(changes[i].name);
                if (pkg == NULL) {
                    return -1;
                }
                list_add(&cache->packages, &pkg->list_header);
            } break;
            case RECIPE_CACHE_CHANGE_UPDATED: {
                // todo
            } break;
            case RECIPE_CACHE_CHANGE_REMOVED: {
                struct list_item* j;
                list_foreach(&cache->packages, j) {
                    struct recipe_cache_package* toCheck = (struct recipe_cache_package*)i;
                    if (strcmp(toCheck->name, changes[i].name) == 0) {
                        list_remove(&cache->packages, &toCheck->list_header);
                        free((char*)toCheck->name);
                        free(toCheck);
                        break;
                    }
                }
            } break;
        }
    }

    status = __save_cache(&g_cache);
    if (status) {
        VLOG_FATAL("cache", "failed to save cache\n");
        return status;
    }
    return 0;
}
