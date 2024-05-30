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

#include <chef/platform.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <utils.h>

static int __contains_envkey(struct list* list, const char* key)
{
    struct list_item* item;

    list_foreach(list, item) {
        struct chef_keypair_item* keypair = (struct chef_keypair_item*)item;
        char*                     end     = strchr(key, '=');

        // we need to make sure lengths are equal as well to avoid false positives
        if (end != NULL && strncmp(keypair->key, key, end - key) == 0 && strlen(keypair->key) == (end - key)) {
            return 1;
        }
    }
    return 0;
}

char** oven_environment_create(const char* const* parent, struct list* additional)
{
    struct list_item* item;
    char**            environment;
    int               entryCount = additional->count;
    int               i = 0;
    int               j = 0; // keeps track of index into environment

    // unreadable mess that simply uses 'entryCount' as an
    // iterator from 0.
    while (parent[entryCount - additional->count]) {
        entryCount++;
    }

    environment = (char**)calloc(entryCount + 1, sizeof(char*));
    if (!environment) {
        return NULL;
    }

    // copy all variables over, but we skip those that are provided in additional
    // list, as we want to use that one instead
    while (parent[i]) {
        if (!__contains_envkey(additional, parent[i])) {
            environment[j++] = strdup(parent[i]);
        }
        i++;
    }

    list_foreach(additional, item) {
        struct chef_keypair_item* keypair    = (struct chef_keypair_item*)item;
        size_t                    lineLength = strlen(keypair->key) + strlen(keypair->value) + 2;
        char*                     line       = (char*)calloc(lineLength, sizeof(char));
        if (!line) {
            return NULL;
        }

        sprintf(line, "%s=%s", keypair->key, keypair->value);
        environment[j++] = line;
    }
    
    return environment;
}

void oven_environment_destroy(char** environment)
{
    int i = 0;
    
    if (!environment) {
        return;
    }

    while (environment[i]) {
        free(environment[i]);
        i++;
    }
    free((void*)environment);
}
