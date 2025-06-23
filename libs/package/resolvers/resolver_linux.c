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
#include "resolvers.h"
#include <chef/platform.h>
#include <chef/list.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vlog.h>

#ifdef CHEF_ON_LINUX
 
struct __path_entry {
    struct list_item list_header;
    const char*      path;
};

// list of library paths on the systen
static const char* g_systemPaths[] = {
    "/usr/local/lib",
    "/usr/local/lib64",
    "/usr/lib",
    "/usr/lib64",
    "/lib",
    "/lib64",
    NULL
};

static int __get_ld_conf_paths(const char* path, struct list* paths)
{
    FILE* file;
    char  buffer[1024];
    int   result = 0;
    VLOG_DEBUG("resolve", "reading ld.conf from %s\n", path);
    
    file = fopen(path, "r");
    if (file == NULL) {
        return -1;
    }
    
    while (fgets(buffer, sizeof(buffer), file) != NULL) {
        struct __path_entry* entry;
        char*                line;

        // skip comments
        if (buffer[0] == '#') {
            continue;
        }

        // remove newline at end of line
        line = strchr(buffer, '\n');
        if (line != NULL) {
            *line = '\0';
        }
        
        // skip empty lines
        if (strlen(buffer) == 0) {
            continue;
        }
        
        // add the path to the list
        entry = calloc(1, sizeof(struct __path_entry));
        if (entry == NULL) {
            result = -1;
            break;
        }

        entry->path = platform_strdup(buffer);
        list_add(paths, &entry->list_header);
    }
    
    fclose(file);
    return result;
}

static const char* __get_platform(struct bake_resolve* resolve)
{
    switch (resolve->arch) {
        case BAKE_RESOLVE_ARCH_X86_64:
            return "x86_64-linux-gnu";
        case BAKE_RESOLVE_ARCH_X86:
            return "i386-linux-gnu";
        case BAKE_RESOLVE_ARCH_ARM:
            return "arm-linux-gnueabi";
        case BAKE_RESOLVE_ARCH_ARM64:
            return "aarch64-linux-gnu";
        case BAKE_RESOLVE_ARCH_MIPS:
            return "mips-linux-gnu";
        case BAKE_RESOLVE_ARCH_MIPS64:
            return "mips64-linux-gnu";
        case BAKE_RESOLVE_ARCH_PPC:
            return "powerpc-linux-gnu";
        case BAKE_RESOLVE_ARCH_PPC64:
            return "powerpc64-linux-gnu";
        case BAKE_RESOLVE_ARCH_SPARC:
            return "sparc-linux-gnu";
        case BAKE_RESOLVE_ARCH_SPARV9:
            return "sparc64-linux-gnu";
        case BAKE_RESOLVE_ARCH_S390:
            return "s390-linux-gnu";
        default:
            return "unknown";
    }
}

static int __load_ld_so_conf_for_platform(const char* sysroot, struct bake_resolve* resolve, struct list* libraryPaths)
{
    char* path;
    int   status = 0;

    path = malloc(PATH_MAX);
    if (path == NULL) {
        errno = ENOMEM;
        return -1;
    }

    snprintf(path, PATH_MAX, "%s/etc/ld.so.conf.d/%s.conf", sysroot, __get_platform(resolve));

    // now try the formatted path first, otherwise we try the ld.so.conf file
    status = __get_ld_conf_paths(path, libraryPaths);
    if (status) {
        printf("oven: %s did not exist, trying /etc/ld.so.conf\n", path);
        snprintf(path, PATH_MAX, "%s/etc/ld.so.conf", sysroot);
        status = __get_ld_conf_paths(path, libraryPaths);
    }
    free(path);
    return status;
}

const char* resolve_platform_dependency(const char* sysroot, struct bake_resolve* resolve, const char* dependency)
{
    struct list          libraryPaths = { 0 };
    struct list_item*    item;
    struct platform_stat stats;
    int                  status;
    char*                path;

    path = malloc(PATH_MAX);
    if (path == NULL) {
        errno = ENOMEM;
        return NULL;
    }

    // Try to resolve the library using the traditional library paths on linux
    // we have to take into account whether paths like 'lib/x86_64-linux-gnu' exists
    // depending on the architecture we have built for.
    status = __load_ld_so_conf_for_platform(sysroot, resolve, &libraryPaths);
    if (status == 0) {
        // Iterate over the library paths and try to resolve the dependency
        list_foreach(&libraryPaths, item) {
            struct __path_entry* entry = (struct __path_entry*)item;
            snprintf(path, PATH_MAX, "%s%s/%s", sysroot, entry->path, dependency);
            if (platform_stat(path, &stats) == 0) {
                return path;
            }
        }
    }

    // Iterate over the default system library paths and try to resolve the dependency
    for (int i = 0; g_systemPaths[i] != NULL; i++) {
        snprintf(path, PATH_MAX, "%s%s/%s", sysroot, g_systemPaths[i], dependency);
        if (platform_stat(path, &stats) == 0) {
            return path;
        }
    }

    free(path);
    return NULL;
}

struct __system_libraries {
    const char* name;
};

static const struct __system_libraries g_ubuntu24_libraries[] = {
    "ld-linux-x86-64.so.2",
    "linux-vdso.so.1",
    "libc.so.6",
    "libm.so.6",
    "libstdc++.so.6",
    "libatomic.so.1",
    "libicudata.so.74",
    "libicui18n.so.74",
    "libicuio.so.74",
    "libicutest.so.74",
    "libicutu.so.74",
    "libicuuc.so.74",
    "libffi.so.8",
    NULL
};

int resolve_is_system_library(const char* base, const char* dependency)
{
    const struct __system_libraries* libraries = NULL;
    VLOG_DEBUG("resolver", "resolve_is_system_library(base=%s, dep=%s)\n", base, dependency);

    if (strcmp(base, "ubuntu-24") == 0) {
        libraries = g_ubuntu24_libraries;
    } else {
        VLOG_WARNING("resolver", "no library resolver for: %s\n", base);
        return 0;
    }

    for (int i = 0; libraries[i].name != NULL; i++) {
        if (strcmp(libraries[i].name, dependency) == 0) {
            return 1;
        }
    }
    return 0;
}

#endif //CHEF_ON_LINUX
