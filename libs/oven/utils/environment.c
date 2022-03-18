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

#include <liboven.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <utils.h>

char** oven_environment_create(const char** parent, struct list* additional)
{
    struct list_item* item;
    char**            environment;
    int               entryCount = additional->count;
    int               i = 0;

    while (parent[entryCount - additional->count]) {
        entryCount++;
    }

    environment = (char**)calloc(entryCount + 1, sizeof(char*));
    if (!environment) {
        return NULL;
    }

    while (parent[i]) {
        environment[i] = strdup(parent[i]);
        i++;
    }

    list_foreach(additional, item) {
        struct oven_keypair_item* keypair    = (struct oven_keypair_item*)item;
        size_t                    lineLength = strlen(keypair->key) + strlen(keypair->value) + 2;
        char*                     line       = (char*)calloc(lineLength, sizeof(char));
        if (!line) {
            return NULL;
        }

        sprintf(line, "%s=%s", keypair->key, keypair->value);
        environment[i++] = line;
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
