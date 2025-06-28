/**
 * Copyright, Philip Meulengracht
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
#include <chef/pack.h>
#include <chef/platform.h>
#include <chef/recipe.h>
#include "resolvers/resolvers.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vlog.h>

struct __resolve_options {
    const char* sysroot;
    const char* install_root;
    const char* ingredients_root;
    int         cross_compiling;
};

static int __verify_commands(struct list* commands, const char* root)
{
    struct list_item* item;
    struct platform_stat stats;

    if (commands->count == 0) {
        return 0;
    }

    list_foreach(commands, item) {
        struct recipe_pack_command* command = (struct recipe_pack_command*)item;
        char*                       path;
        
        if (command->path == NULL || strlen(command->path) == 0) {
            VLOG_ERROR("commands", "command %s has no path\n", command->name);
            return -1;
        }

        // verify the command points to something correct
        path = strpathcombine(root, command->path);
        if (path == NULL) {
            VLOG_ERROR("commands", "failed to combine command path\n");
            return -1;
        }

        if (platform_stat(path, &stats)) {
            VLOG_ERROR("commands", "could not find command path %s\n", path);
            free(path);
            return -1;
        }
        free(path);
    }
    return 0;
}

static int __resolve_dependency_path(struct bake_resolve* resolve, struct bake_resolve_dependency* dependency, struct __resolve_options* options)
{
    struct list       files = { 0 };
    struct list_item* item;
    int               status;

    // priority 1 - check in install path
    status = platform_getfiles(options->install_root, 1, &files);
    if (status) {
        VLOG_ERROR("commands", "resolve: failed to get install file list\n");
        return -1;
    }

    list_foreach(&files, item) {
        struct platform_file_entry* file = (struct platform_file_entry*)item;
        if (!strcmp(file->name, dependency->name)) {
            dependency->path = platform_strdup(file->path);
            dependency->sub_path = platform_strdup(file->sub_path);
            platform_getfiles_destroy(&files);
            return 0;
        }
    }
    platform_getfiles_destroy(&files);

    // priority 2 - maybe it comes from build ingredients
    status = platform_getfiles(options->ingredients_root, 1, &files);
    if (status) {
        VLOG_ERROR("commands", "resolve: failed to get install file list\n");
        return -1;
    }

    list_foreach(&files, item) {
        struct platform_file_entry* file = (struct platform_file_entry*)item;
        if (!strcmp(file->name, dependency->name)) {
            dependency->path = platform_strdup(file->path);
            dependency->sub_path = platform_strdup(file->sub_path);
            platform_getfiles_destroy(&files);
            return 0;
        }
    }
    platform_getfiles_destroy(&files);
    
    // priority 3 - invoke platform resolver (if allowed)
    // we cannot do this if we are cross-compiling - we do not
    // necessarily know the layout of that.
    if (!options->cross_compiling) {
        const char* path;

        if (resolve_is_system_library("ubuntu-24", dependency->name)) {
            // okay library is carried by the system, we can safely ignore it
            dependency->ignored = 1;
            return 0;
        }

        path = resolve_platform_dependency(options->sysroot, resolve, dependency->name);
        if (path) {
            dependency->path = path;
            dependency->system_library = 1;
            return 0;
        }
    }
    errno = ENOENT;
    return -1;
}

static int __resolve_elf_dependencies(struct bake_resolve* resolve, struct __resolve_options* options)
{
    while (1) {
        struct list_item* item;
        int               resolved = 0;

        list_foreach(&resolve->dependencies, item) {
            struct bake_resolve_dependency* dependency = (struct bake_resolve_dependency*)item;
            if (!dependency->resolved) {
                int status;
                
                // try to resolve the location of the dependency
                status = __resolve_dependency_path(resolve, dependency, options);
                if (status) {
                    VLOG_ERROR("commands", "resolve: failed to locate %s\n", dependency->name);
                    return -1;
                }

                // now we resolve the dependencies of this binary
                if (!dependency->ignored) {
                    status = elf_resolve_dependencies(dependency->path, &resolve->dependencies);
                    if (status != 0) {
                        VLOG_ERROR("commands", "failed to resolve dependencies for %s\n", dependency->name);
                        return -1;
                    }
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

static int __resolve_pe_dependencies(struct bake_resolve* resolve, struct __resolve_options* options)
{
    while (1) {
        struct list_item* item;
        int               resolved = 0;

        list_foreach(&resolve->dependencies, item) {
            struct bake_resolve_dependency* dependency = (struct bake_resolve_dependency*)item;
            if (!dependency->resolved) {
                int status;
                
                // try to resolve the location of the dependency
                status = __resolve_dependency_path(resolve, dependency, options);
                if (status) {
                    VLOG_ERROR("commands", "failed to locate %s\n", dependency->name);
                    return -1;
                }

                // now we resolve the dependencies of this binary
                if (dependency->ignored) {
                    status = pe_resolve_dependencies(dependency->path, &resolve->dependencies);
                    if (status != 0) {
                        VLOG_ERROR("commands", "failed to resolve dependencies for %s\n", dependency->name);
                        return -1;
                    }
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

static int __resolve_command(struct recipe_pack_command* command, struct list* resolves, struct __pack_resolve_commands_options* options)
{
    struct bake_resolve* resolve;
    const char*             path;
    int                     status;

    // verify the command points to something correct
    path = strpathcombine(options->install_root, command->path);
    if (path == NULL) {
        VLOG_ERROR("commands", "failed to combine command path\n");
        return -1;
    }

    resolve = (struct bake_resolve*)calloc(1, sizeof(struct bake_resolve));
    resolve->path = path;

    if (elf_is_valid(path, &resolve->arch) == 0) {
        status = elf_resolve_dependencies(path, &resolve->dependencies);
        if (!status) {
            status = __resolve_elf_dependencies(resolve, &(struct __resolve_options) {
                .sysroot = options->sysroot,
                .install_root = options->install_root,
                .ingredients_root = options->ingredients_root,
                .cross_compiling = options->cross_compiling
            });
        }
    } else if (pe_is_valid(path, &resolve->arch) == 0) {
        status = pe_resolve_dependencies(path, &resolve->dependencies);
        if (!status) {
            status = __resolve_pe_dependencies(resolve, &(struct __resolve_options) {
                .sysroot = options->sysroot,
                .install_root = options->install_root,
                .ingredients_root = options->ingredients_root,
                .cross_compiling = options->cross_compiling
            });
        }
    } else {
        status = -1;
    }

    if (status != 0) {
        VLOG_ERROR("commands", "failed to resolve dependencies for command %s\n", command->name);
        free(resolve);
        return -1;
    }

    list_add(resolves, &resolve->list_header);
    return 0;
}

static int __resolve_commands(struct list* commands, struct list* resolves, struct __pack_resolve_commands_options* options)
{
    struct list_item* item;
    int               status;

    if (commands->count == 0) {
        return 0;
    }

    // Iterate over all commands and resolve their dependencies
    list_foreach(commands, item) {
        struct recipe_pack_command* command = (struct recipe_pack_command*)item;
        status = __resolve_command(command, resolves, options);
        if (status) {
            return status;
        }
    }
    return 0;
}

int pack_resolve_commands(struct list* commands, struct list* resolves, struct __pack_resolve_commands_options* options)
{
    int status;

    status = __verify_commands(commands, options->install_root);
    if (status) {
        VLOG_ERROR("commands", "failed to verify commands\n");
        return -1;
    }
    return __resolve_commands(commands, resolves, options);
}

static void __cleanup_dependencies(struct list* dependencies)
{
    struct list_item* item;

    for (item = dependencies->head; item != NULL;) {
        struct bake_resolve_dependency* dependency = (struct bake_resolve_dependency*)item;
        item = item->next;

        free((void*)dependency->name);
        free((void*)dependency->path);
        free((void*)dependency->sub_path);
        free(dependency);
    }
    list_init(dependencies);
}

void pack_resolve_destroy(struct list* resolves)
{
    struct list_item* item;

    if (resolves == NULL) {
        return;
    }

    for (item = resolves->head; item != NULL;) {
        struct bake_resolve* resolve = (struct bake_resolve*)item;
        item = item->next;

        free((void*)resolve->path);
        free(resolve);
    }
    list_init(resolves);
}
