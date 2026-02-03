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

#include <chef/platform.h>
#include <ctype.h>
#include <string.h>
#include <vlog.h>

#include "resolvers.h"

extern const char* resolve_platform_dependency_linux(const char* sysroot, struct bake_resolve* resolve, const char* dependency);
extern int         resolve_is_system_library_linux(const char* base, const char* dependency);

extern const char* resolve_platform_dependency_windows(const char* sysroot, struct bake_resolve* resolve, const char* dependency);
extern int         resolve_is_system_library_windows(const char* base, const char* dependency);

static int __is_windows_target(const char* platform, const char* dependency)
{
    VLOG_DEBUG("resolver", "__is_windows_target(platform=%s, dep=%s)\n",
        platform ? platform : "(null)",
        dependency ? dependency : "(null)"
    );
    
    if (platform != NULL && strcmp(platform, "windows") == 0) {
        return 1;
    }
    
    // PE imports are typically *.dll; this allows correct resolution even when the
    // caller doesn't provide a platform string.
    if (dependency != NULL && strendswith(dependency, ".dll")) {
        return 1;
    }
    return 0;
}

const char* resolve_platform_dependency(const char* sysroot, const char* platform, struct bake_resolve* resolve, const char* dependency)
{
    VLOG_DEBUG("resolver", "resolve_platform_dependency(sysroot=%s, platform=%s, dep=%s)\n",
        sysroot ? sysroot : "",
        platform ? platform : "(null)",
        dependency ? dependency : "(null)"
    );
    
    if (__is_windows_target(platform, dependency)) {
        return resolve_platform_dependency_windows(sysroot, resolve, dependency);
    }
    return resolve_platform_dependency_linux(sysroot, resolve, dependency);
}

int resolve_is_system_library(const char* base, const char* dependency)
{
    VLOG_DEBUG("resolver", "resolve_is_system_library(base=%s, dep=%s)\n",
        base ? base : "(null)",
        dependency ? dependency : "(null)"
    );
    
    // dispatch based on base tag (preferred), falling back to dependency patterns.
    if (base != NULL) {
        if (strstr(base, "servercore") != NULL || strstr(base, "windows") != NULL) {
            return resolve_is_system_library_windows(base, dependency);
        }
        if (strstr(base, "ubuntu") != NULL) {
            return resolve_is_system_library_linux(base, dependency);
        }
    }

    if (dependency != NULL && strendswith(dependency, ".dll")) {
        return resolve_is_system_library_windows(base, dependency);
    }

    VLOG_WARNING("resolver", "no system library resolver for base=%s\n", base ? base : "(null)");
    return 0;
}
