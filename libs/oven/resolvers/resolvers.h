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

#ifndef __RESOLVERS_H__
#define __RESOLVERS_H__

#include <list.h>

enum oven_resolve_arch {
    OVEN_RESOLVE_ARCH_UNKNOWN,
    OVEN_RESOLVE_ARCH_X86,
    OVEN_RESOLVE_ARCH_X86_64,
    OVEN_RESOLVE_ARCH_ARM,
    OVEN_RESOLVE_ARCH_ARM64,
    OVEN_RESOLVE_ARCH_MIPS,
    OVEN_RESOLVE_ARCH_MIPS64,
    OVEN_RESOLVE_ARCH_PPC,
    OVEN_RESOLVE_ARCH_PPC64,
    OVEN_RESOLVE_ARCH_SPARC,
    OVEN_RESOLVE_ARCH_SPARV9,
    OVEN_RESOLVE_ARCH_S390,

    OVEN_RESOLVE_ARCH_MAX
};

struct oven_resolve_dependency {
    struct list_item list_header;
    const char*      name;
    const char*      path;
    int              resolved;
};

struct oven_resolve {
    struct list_item       list_header;
    const char*            path;
    enum oven_resolve_arch arch;
    struct list            dependencies;
};

/**
 * @brief 
 * 
 * @param path 
 * @return int 
 */
extern int elf_is_valid(const char* path, enum oven_resolve_arch* arch);

/**
 * @brief Resolves all dependencies for the given binary. The binary must be an
 * executable or a dynamic library. This is not a recursive function. All recursive
 * dependencies must be resolved as well.
 * 
 * @param[In] path         The path to the binary to resolve dependencies for 
 * @param[In] dependencies The list to store the dependencies in
 * @return int             0 on success, -1 on error
 */
extern int elf_resolve_dependencies(const char* path, struct list* dependencies);

/**
 * @brief Tries to resolve where the dependency is located on the system.
 * 
 * @param[In] resolve    The primary binary that requires this dependency
 * @param[In] dependency The dependency to resolve
 * @return const char*   The full path of the resolved dependency
 */
extern const char* resolve_platform_dependency(struct oven_resolve* resolve, const char* dependency);

#endif //!__RESOLVERS_H__
