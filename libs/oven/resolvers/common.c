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

#include "resolvers.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int __resolve_add_dependency(struct list* dependencies, const char* library)
{
    struct oven_resolve_dependency* dependency;
    struct list_item*               item;

    // make sure dependency is not already added
    list_foreach(dependencies, item) {
        dependency = (struct oven_resolve_dependency*)item;
        if (strcmp(dependency->name, library) == 0) {
            return 0;
        }
    }

    // add dependency
    dependency = calloc(1, sizeof(struct oven_resolve_dependency));
    if (dependency == NULL) {
        return -1;
    }

    dependency->name = platform_strdup(library);
    if (dependency->name == NULL) {
        free(dependency);
        return -1;
    }

    list_add(dependencies, &dependency->list_header);
    return 0;
}

int __resolve_load_file(const char* path, void** bufferOut, size_t* sizeOut)
{
    FILE*  file;
    void*  buffer;
    size_t size;
    size_t read;
    
    file = fopen(path, "rb");
    if (file == NULL) {
        return -1;
    }
    
    fseek(file, 0, SEEK_END);
    size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    buffer = malloc(size);
    if (buffer == NULL) {
        fclose(file);
        return -1;
    }
    
    read = fread(buffer, 1, size, file);
    if (read != size) {
        fclose(file);
        free(buffer);
        return -1;
    }
    fclose(file);
    
    *bufferOut = buffer;
    *sizeOut = size;
    return 0;
}
