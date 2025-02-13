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

struct recipe_cache_package {
    struct list_item list_header;
    const char*      name;
};

static struct recipe_cache_package* __parse_recipe_cache_package(const json_t* packageItem)
{
    json_t* member;
    struct recipe_cache_package* pkg = malloc(sizeof(struct recipe_cache_package));
    if (pkg == NULL) {
        return NULL;
    }

    memset(pkg, 0, sizeof(struct recipe_cache_package));

    member = json_object_get(packageItem, "name");
    if (member == NULL) {
        free(pkg);
        return NULL;
    }
    pkg->name = platform_strdup(json_string_value(member));
    return pkg;
}

static json_t* __serialize_recipe_cache_package(struct recipe_cache_package* pkg)
{
    json_t* root;
    VLOG_DEBUG("cache", "__serialize_recipe_cache_package(name=%s)\n", pkg->name);
    
    root = json_object();
    if (!root) {
        return NULL;
    }

    json_object_set_new(root, "name", json_string(pkg->name));
    return root;
}

static struct recipe_cache_package* __new_recipe_cache_package(const char* name)
{
    struct recipe_cache_package* pkg = malloc(sizeof(struct recipe_cache_package));
    if (pkg == NULL) {
        return NULL;
    }
    memset(pkg, 0, sizeof(struct recipe_cache_package));
    
    pkg->name = platform_strdup(name);
    if (pkg->name == NULL) {
        free(pkg);
        return NULL;
    }

    return pkg;
}

static void __delete_recipe_cache_package(struct recipe_cache_package* pkg)
{
    free((void*)pkg->name);
    free(pkg);
}

struct recipe_cache_ingredient {
    struct list_item list_header;
    const char*      name;
};

static struct recipe_cache_ingredient* __parse_recipe_cache_ingredient(const json_t* packageItem)
{
    json_t* member;
    struct recipe_cache_ingredient* ing = malloc(sizeof(struct recipe_cache_ingredient));
    if (ing == NULL) {
        return NULL;
    }

    memset(ing, 0, sizeof(struct recipe_cache_ingredient));

    member = json_object_get(packageItem, "name");
    if (member == NULL) {
        free(ing);
        return NULL;
    }
    ing->name = platform_strdup(json_string_value(member));
    return ing;
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

static void __delete_recipe_cache_ingredient(struct recipe_cache_ingredient* ing)
{
    free((void*)ing->name);
    free(ing);
}

struct recipe_cache_item {
    const char* name;
    const char* uuid;
    struct list packages;    // list<recipe_cache_package>
    struct list ingredients; // list<recipe_cache_ingredient>
    json_t*     keystore;
};

static const char* g_uuidFmt = "xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx";
static const char* g_hex = "0123456789ABCDEF-";

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

static int __construct_recipe_cache_item(struct recipe_cache_item* cacheItem, const char* name)
{
    char uuid[40];
    VLOG_DEBUG("cache", "__construct_recipe_cache_item(name=%s)\n", name);

    memset(cacheItem, 0, sizeof(struct recipe_cache_item));
    __generate_cache_uuid(&uuid[0]);
    cacheItem->name = platform_strdup(name);
    cacheItem->uuid = platform_strdup(&uuid[0]);
    cacheItem->keystore = json_object();
    if (cacheItem->name == NULL || cacheItem->uuid == NULL || cacheItem->keystore == NULL) {
        free((void*)cacheItem->name);
        free((void*)cacheItem->uuid);
        json_decref(cacheItem->keystore);
        return -1;
    }
    return 0;
}

static int __parse_cache_item(struct recipe_cache_item* cacheItem, json_t* root)
{
    json_t* member;
    size_t  length;
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

    member = json_object_get(root, "packages");
    if (member == NULL) {
        return -1;
    }

    length = json_array_size(member);
    if (length) {
        for (size_t i = 0; i < length; i++) {
            json_t* packageItem = json_array_get(member, i);
            struct recipe_cache_package* pkg = __parse_recipe_cache_package(packageItem);
            if (pkg == NULL) {
                return -1;
            }
            list_add(&cacheItem->packages, &pkg->list_header);
        }
    }

    member = json_object_get(root, "ingredients");
    if (member == NULL) {
        return -1;
    }

    length = json_array_size(member);
    if (length) {
        for (size_t i = 0; i < length; i++) {
            json_t* ingredientItem = json_array_get(member, i);
            struct recipe_cache_ingredient* ing = __parse_recipe_cache_ingredient(ingredientItem);
            if (ing == NULL) {
                return -1;
            }
            list_add(&cacheItem->ingredients, &ing->list_header);
        }
    }

    return 0;
}

static json_t* __serialize_cache_item_packages(struct list* packages)
{
    struct list_item* i;
    json_t*           items;
    VLOG_DEBUG("cache", "__serialize_cache_item_packages(count=%i)\n", packages->count);

    items = json_array();
    if (!items) {
        return NULL;
    }

    list_foreach(packages, i) {
        json_t* pkg = __serialize_recipe_cache_package((struct recipe_cache_package*)i);
        if (pkg == NULL) {
            json_decref(items);
            return NULL;
        }
        json_array_append_new(items, pkg);
    }
    return items;
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
    VLOG_DEBUG("cache", "__serialize_cache_item(cache=%s)\n", cacheItem->name);
    
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
    json_object_set(root, "cache", cacheItem->keystore);
    return root;
}

struct recipe_cache {
    struct recipe*            current;
    const char*               path;

    struct recipe_cache_item* items;
    int                       item_count;

    int                       xaction;
};

static struct recipe_cache* __recipe_cache_new(const char* path, struct recipe* recipe)
{
    struct recipe_cache* cache = calloc(1, sizeof(struct recipe_cache));
    if (cache == NULL) {
        return NULL;
    }

    cache->path = platform_strdup(path);
    if (cache->path == NULL) {
        free(cache);
        return NULL;
    }

    cache->current = recipe;
    return cache;
}

static void __recipe_cache_delete(struct recipe_cache* cache)
{
    if (cache == NULL) {
        return;
    }

    free((void*)cache->path);
    free(cache);
}

static int __parse_cache(struct recipe_cache* cache, json_t* root)
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
    cache->items = calloc(length, sizeof(struct recipe_cache_item));
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

static int __load_config(struct recipe_cache* cache, const char* path)
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

static json_t* __serialize_cache(struct recipe_cache* cache)
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

static int __save_cache(struct recipe_cache* cache)
{
    json_t* root;
    VLOG_DEBUG("cache", "__save_cache(cache=%s)\n", cache->path);

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

static int __ensure_recipe_cache(struct recipe_cache* cache, struct recipe* current)
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

    expanded = realloc(cache->items, sizeof(struct recipe_cache_item) * (cache->item_count + 1));
    if (expanded == NULL) {
        return -1;
    }
    if (__construct_recipe_cache_item(&(((struct recipe_cache_item*)expanded)[cache->item_count]), current->project.name)) {
        return -1;
    }

    cache->items = expanded;
    cache->item_count++;
    return 0;
}

// API
int recipe_cache_create(struct recipe* current, const char* cwd, struct recipe_cache** cacheOut)
{
    struct recipe_cache* cache;
    char                 buff[PATH_MAX] = { 0 };
    int                  status;

    snprintf(&buff[0], sizeof(buff), "%s" CHEF_PATH_SEPARATOR_S ".vchcache", cwd);

    // initialize the random counter for guid's
    srand(clock());

    // create a new cache
    cache = __recipe_cache_new(&buff[0], current);
    if (cache == NULL) {
        VLOG_ERROR("cache", "out of memory for cache allocation!\n");
        return -1;
    }

    status = __load_config(cache, &buff[0]);
    if (status) {
        VLOG_ERROR("cache", "failed to load or initialize the recipe cache\n");
        __recipe_cache_delete(cache);
        return status;
    }

    status = __ensure_recipe_cache(cache, current);
    if (status) {
        VLOG_ERROR("cache", "failed to ensure cache for current recipe\n");
        __recipe_cache_delete(cache);
        return status;
    }
    *cacheOut = cache;
    return 0;
}

const char* recipe_cache_uuid_for(struct recipe_cache* cache, const char* name)
{
    for (int i = 0; i < cache->item_count; i++) {
        if (strcmp(cache->items[i].name, name) == 0) {
            return cache->items[i].uuid;
        }
    }

    VLOG_FATAL("cache", "no cache entry for %s\n", name);
    return NULL;
}

const char* recipe_cache_uuid(struct recipe_cache* cache)
{
    if (cache->current != NULL) {
        return recipe_cache_uuid_for(cache, cache->current->project.name);
    }

    VLOG_FATAL("cache", "no recipe specified\n");
    return NULL;
}

static struct recipe_cache_item* __get_cache_item(struct recipe_cache* cache)
{
    if (cache->current == NULL) {
        VLOG_FATAL("cache", "__get_cache_item: invoked but no recipe set\n");
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

static void __clear_packages(struct list* packages)
{
    struct list_item* i;
    while ((i = packages->head)) {
        struct recipe_cache_package* pkg = (struct recipe_cache_package*)i;
        packages->head = i->next;
        __delete_recipe_cache_package(pkg);
    }
}

static void __clear_ingredients(struct list* ingredients)
{
    struct list_item* i;
    while ((i = ingredients->head)) {
        struct recipe_cache_ingredient* ing = (struct recipe_cache_ingredient*)i;
        ingredients->head = i->next;
        __delete_recipe_cache_ingredient(ing);
    }
}

int recipe_cache_clear_for(struct recipe_cache* cache, const char* name)
{
    if (!cache->xaction) {
        VLOG_FATAL("cache", "recipe_cache_clear_for: no transaction\n");
    }

    for (int i = 0; i < cache->item_count; i++) {
        if (strcmp(cache->items[i].name, name) == 0) {
            __clear_packages(&cache->items[i].packages);
            __clear_ingredients(&cache->items[i].ingredients);
            json_object_clear(cache->items[i].keystore);
            return 0;
        }
    }
    errno = ENOENT;
    return -1;
}

void recipe_cache_transaction_begin(struct recipe_cache* cache)
{
    if (cache->xaction != 0) {
        VLOG_FATAL("cache", "transaction already in progress\n");
    }
    cache->xaction = 1;
}

void recipe_cache_transaction_commit(struct recipe_cache* cache)
{
    if (!cache->xaction) {
        VLOG_FATAL("cache", "no transaction in progress\n");
    }

    if (__save_cache(cache)) {
        VLOG_FATAL("cache", "failed to commit changes to cache\n");
    }
    cache->xaction = 0;
}

const char* recipe_cache_key_string(struct recipe_cache* cache, const char* key)
{
    struct recipe_cache_item* cacheItem = __get_cache_item(cache);
    return json_string_value(json_object_get(cacheItem->keystore, key));
}

int recipe_cache_key_set_string(struct recipe_cache* cache, const char* key, const char* value)
{
    struct recipe_cache_item* cacheItem = __get_cache_item(cache);
    int                       status;

    if (!cache->xaction) {
        VLOG_FATAL("cache", "recipe_cache_key_set_string: no transaction\n");
    }

    status = json_object_set_new(cacheItem->keystore, key, json_string(value));
    if (status) {
        VLOG_ERROR("cache", "failed to update value %s for %s: %i\n", key, value, status);
    }
    return status;
}

int recipe_cache_key_bool(struct recipe_cache* cache, const char* key)
{
    const char* value = recipe_cache_key_string(cache, key);
    if (value == NULL) {
        return 0;
    }
    return strcmp(value, "true") == 0 ? 1 : 0;
}

int recipe_cache_key_set_bool(struct recipe_cache* cache, const char* key, int value)
{
    return recipe_cache_key_set_string(cache, key, value ? "true" : "false");
}

int recipe_cache_is_part_sourced(struct recipe_cache* cache, const char* part)
{
    char buffer[256];
    snprintf(&buffer[0], sizeof(buffer), "%s-sourced", part);
    return recipe_cache_key_bool(cache, &buffer[0]);
}

int recipe_cache_mark_part_sourced(struct recipe_cache* cache, const char* part)
{
    char buffer[256];
    snprintf(&buffer[0], sizeof(buffer), "%s-sourced", part);
    return recipe_cache_key_set_bool(cache, &buffer[0], 1);
}

int recipe_cache_mark_step_complete(struct recipe_cache* cache, const char* part, const char* step)
{
    char buffer[256];
    snprintf(&buffer[0], sizeof(buffer), "%s-%s", part, step);
    return recipe_cache_key_set_bool(cache, &buffer[0], 1);
}

int recipe_cache_mark_step_incomplete(struct recipe_cache* cache, const char* part, const char* step)
{
    char buffer[256];
    snprintf(&buffer[0], sizeof(buffer), "%s-%s", part, step);
    return recipe_cache_key_set_bool(cache, &buffer[0], 0);
}

int recipe_cache_is_step_complete(struct recipe_cache* cache, const char* part, const char* step)
{
    char buffer[256];
    snprintf(&buffer[0], sizeof(buffer), "%s-%s", part, step);
    return recipe_cache_key_bool(cache, &buffer[0]);
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

int recipe_cache_calculate_package_changes(struct recipe_cache* cache, struct recipe_cache_package_change** changes, int* changeCount)
{
    struct recipe_cache_item* cacheItem = NULL;
    struct list_item          *i, *j;
    int                       capacity = 0;
    VLOG_DEBUG("cache", "recipe_cache_calculate_package_changes()\n");

    *changes = NULL;
    *changeCount = 0;

    if (cache != NULL) {
        cacheItem = __get_cache_item(cache);
    }

    // We use an insanely inefficient algorithm here, but we don't care as
    // these lists should never be long, and we do not have access to an easy
    // hashtable here. 

    // check packages added
    list_foreach(&cache->current->environment.host.packages, i) {
        struct list_item_string* toCheck = (struct list_item_string*)i;
        int                      exists = 0;

        if (cacheItem != NULL) {
            list_foreach(&cacheItem->packages, j) {
                struct recipe_cache_package* pkg = (struct recipe_cache_package*)i;
                if (strcmp(toCheck->value, pkg->name) == 0) {
                    // found, not a new package
                    exists = 1;
                    break;
                }
            }
        }

        if (!exists) {
            if(__add_package_change(changes, changeCount, &capacity, toCheck->value, RECIPE_CACHE_CHANGE_ADDED)) {
                return -1;
            }
        }
    }

    // check packages removed if the cache is there
    if (cacheItem != NULL) {
        list_foreach(&cacheItem->packages, i) {
            struct list_item_string* toCheck = (struct list_item_string*)i;
            int                     exists = 0;
            list_foreach(&cache->current->environment.host.packages, j) {
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
    }
    return 0;
}

int recipe_cache_commit_package_changes(struct recipe_cache* cache, struct recipe_cache_package_change* changes, int count)
{
    struct recipe_cache_item* cacheItem = __get_cache_item(cache);
    VLOG_DEBUG("cache", "recipe_cache_commit_package_changes(count=%i)\n", count);
    
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
                list_add(&cacheItem->packages, &pkg->list_header);
            } break;
            case RECIPE_CACHE_CHANGE_UPDATED: {
                // todo
            } break;
            case RECIPE_CACHE_CHANGE_REMOVED: {
                struct list_item* j;
                list_foreach(&cacheItem->packages, j) {
                    struct recipe_cache_package* toCheck = (struct recipe_cache_package*)j;
                    if (strcmp(toCheck->name, changes[i].name) == 0) {
                        list_remove(&cacheItem->packages, &toCheck->list_header);
                        __delete_recipe_cache_package(toCheck);
                        break;
                    }
                }
            } break;
        }
    }
    return 0;
}
