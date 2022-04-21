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
#include <libplatform.h>
#include <liboven.h>
#include "resolvers/resolvers.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern const char* __get_install_path(void);
extern const char* __get_ingredients_path(void);

static int __verify_commands(struct list* commands)
{
    struct list_item* item;
    struct platform_stat stats;

    if (commands->count == 0) {
        return 0;
    }

    list_foreach(commands, item) {
        struct oven_pack_command* command = (struct oven_pack_command*)item;
        char*                     path;
        
        if (command->path == NULL || strlen(command->path) == 0) {
            fprintf(stderr, "oven: command %s has no path\n", command->name);
            return -1;
        }

        // verify the command points to something correct
        path = strpathcombine(__get_install_path(), command->path);
        if (path == NULL) {
            fprintf(stderr, "oven: failed to combine command path\n");
            return -1;
        }

        if (platform_stat(path, &stats)) {
            fprintf(stderr, "oven: could not find command path %s\n", path);
            free(path);
            return -1;
        }
        free(path);
    }
    return 0;
}

static const char* __resolve_dependency_path(struct oven_resolve* resolve, const char* dependency)
{
    struct list       files = { 0 };
    struct list_item* item;
    int               status;

    // priority 1 - check in install path
    status = platform_getfiles(__get_install_path(), &files);
    if (status) {
        fprintf(stderr, "oven: failed to get install file list\n");
        return NULL;
    }

    // priority 2 - check in ingredient path
    status = platform_getfiles(__get_ingredients_path(), &files);
    if (status) {
        fprintf(stderr, "oven: failed to get ingredient file list\n");
        platform_getfiles_destroy(&files);
        return NULL;
    }

    list_foreach(&files, item) {
        struct platform_file_entry* file = (struct platform_file_entry*)item;
        if (!strcmp(file->name, dependency)) {
            char* pathCopy = strdup(file->path);
            platform_getfiles_destroy(&files);
            return pathCopy;
        }
    }
    platform_getfiles_destroy(&files);

    // priority 3 - invoke platform resolver
    return resolve_platform_dependency(resolve, dependency);
}

static int __resolve_elf_dependencies(struct oven_resolve* resolve)
{
    while (1) {
        struct list_item* item;
        int               resolved = 0;

        list_foreach(&resolve->dependencies, item) {
            struct oven_resolve_dependency* dependency = (struct oven_resolve_dependency*)item;
            if (!dependency->resolved) {
                int status;
                
                // try to resolve the location of the dependency
                dependency->path = __resolve_dependency_path(resolve, dependency->name);
                if (dependency->path == NULL) {
                    fprintf(stderr, "oven: failed to locate %s\n", dependency->name);
                    return -1;
                }

                // now we resolve the dependencies of this binary
                status = elf_resolve_dependencies(dependency->path, &resolve->dependencies);
                if (status != 0) {
                    fprintf(stderr, "oven: failed to resolve dependencies for %s\n", dependency->name);
                    return -1;
                }

                dependency->resolved = 1;
                resolved = 1;
            }
        }

        if (!resolved) {
            break;
        }
    }
    return 0;
}

static int __resolve_command(struct oven_pack_command* command, struct list* resolves)
{
    struct oven_resolve* resolve;
    const char*          path;
    int                  status;

    // verify the command points to something correct
    path = strpathcombine(__get_install_path(), command->path);
    if (path == NULL) {
        fprintf(stderr, "oven: failed to combine command path\n");
        return -1;
    }

    resolve = (struct oven_resolve*)calloc(1, sizeof(struct oven_resolve));
    resolve->path = path;

    if (elf_is_valid(path, &resolve->arch) == 0) {
        status = elf_resolve_dependencies(path, &resolve->dependencies);
        if (!status) {
            status = __resolve_elf_dependencies(resolve);
        }
    } else {
        status = -1;
    }

    if (status != 0) {
        fprintf(stderr, "oven: failed to resolve dependencies for command %s\n", command->name);
        free(resolve);
        return -1;
    }

    list_add(resolves, &resolve->list_header);
    return 0;
}

static int __resolve_commands(struct list* commands, struct list* resolves)
{
    struct list_item* item;
    int               status;

    if (commands->count == 0) {
        return 0;
    }

    // Iterate over all commands and resolve their dependencies
    list_foreach(commands, item) {
        struct oven_pack_command* command = (struct oven_pack_command*)item;
        status = __resolve_command(command, resolves);
        if (status) {
            return status;
        }
    }
    return 0;
}

int oven_resolve_commands(struct list* commands, struct list* resolves)
{
    int status;

    status = __verify_commands(commands);
    if (status) {
        fprintf(stderr, "oven: failed to verify commands\n");
        return -1;
    }
    return __resolve_commands(commands, resolves);
}
