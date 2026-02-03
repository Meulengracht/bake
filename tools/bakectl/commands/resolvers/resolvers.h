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

#ifndef __RESOLVERS_H__
#define __RESOLVERS_H__

#include <chef/list.h>

enum bake_resolve_arch {
    BAKE_RESOLVE_ARCH_UNKNOWN,
    BAKE_RESOLVE_ARCH_X86,
    BAKE_RESOLVE_ARCH_X86_64,
    BAKE_RESOLVE_ARCH_ARM,
    BAKE_RESOLVE_ARCH_ARM64,
    BAKE_RESOLVE_ARCH_MIPS,
    BAKE_RESOLVE_ARCH_MIPS64,
    BAKE_RESOLVE_ARCH_PPC,
    BAKE_RESOLVE_ARCH_PPC64,
    BAKE_RESOLVE_ARCH_SPARC,
    BAKE_RESOLVE_ARCH_SPARV9,
    BAKE_RESOLVE_ARCH_S390,
    BAKE_RESOLVE_ARCH_RISCV32,
    BAKE_RESOLVE_ARCH_RISCV64,
    BAKE_RESOLVE_ARCH_RISCV128,

    BAKE_RESOLVE_ARCH_MAX
};

struct bake_resolve_dependency {
    struct list_item list_header;
    const char*      name;
    const char*      path;
    const char*      sub_path; // only set if system_library == 0
    int              resolved;
    int              system_library;
    int              ignored;
};

struct bake_resolve {
    struct list_item       list_header;
    const char*            path;
    enum bake_resolve_arch arch;
    struct list            dependencies;
};

/**
 * @brief 
 * 
 * @param path 
 * @return int 
 */
extern int elf_is_valid(const char* path, enum bake_resolve_arch* arch);
extern int pe_is_valid(const char* path, enum bake_resolve_arch* arch);

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
extern int pe_resolve_dependencies(const char* path, struct list* dependencies);

/**
 * @brief Tries to resolve where the dependency is located on the system.
 * 
 * @param[In] sysroot    The root path of the system to use.
 * @param[In] resolve    The primary binary that requires this dependency
 * @param[In] dependency The dependency to resolve
 * @return const char*   The full path of the resolved dependency
 */
extern const char* resolve_platform_dependency(const char* sysroot, const char* platform, struct bake_resolve* resolve, const char* dependency);

/**
 * @brief Determines whether the library is marked as a system library
 */
extern int resolve_is_system_library(const char* base, const char* dependency);

#endif //!__RESOLVERS_H__
