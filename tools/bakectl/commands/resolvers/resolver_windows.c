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

#include <ctype.h>
#include <errno.h>
#include <chef/platform.h>
#include <chef/list.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vlog.h>

#include "resolvers.h"

static int __ascii_equals_ignore_case(const char* a, const char* b)
{
    if (a == NULL || b == NULL) {
        return 0;
    }

    while (*a && *b) {
        unsigned char ca = (unsigned char)*a++;
        unsigned char cb = (unsigned char)*b++;
        if (tolower(ca) != tolower(cb)) {
            return 0;
        }
    }
    return *a == '\0' && *b == '\0';
}

static int __ascii_starts_with_ignore_case(const char* s, const char* prefix)
{
    if (s == NULL || prefix == NULL) {
        return 0;
    }

    while (*prefix) {
        unsigned char cs = (unsigned char)*s++;
        unsigned char cp = (unsigned char)*prefix++;
        if (tolower(cs) != tolower(cp)) {
            return 0;
        }
    }
    return 1;
}

static int __ascii_ends_with_ignore_case(const char* s, const char* suffix)
{
    size_t sl, sul;

    if (s == NULL || suffix == NULL) {
        return 0;
    }

    sl = strlen(s);
    sul = strlen(suffix);
    if (sl < sul) {
        return 0;
    }
    return __ascii_equals_ignore_case(s + (sl - sul), suffix);
}

static int __ascii_contains_ignore_case(const char* haystack, const char* needle)
{
    size_t needle_len;

    if (haystack == NULL || needle == NULL) {
        return 0;
    }

    needle_len = strlen(needle);
    if (needle_len == 0) {
        return 1;
    }

    for (const char* p = haystack; *p; p++) {
        size_t i;
        for (i = 0; i < needle_len; i++) {
            unsigned char ch;
            unsigned char cn;

            if (p[i] == '\0') {
                return 0;
            }

            ch = (unsigned char)p[i];
            cn = (unsigned char)needle[i];
            if (tolower(ch) != tolower(cn)) {
                break;
            }
        }
        if (i == needle_len) {
            return 1;
        }
    }
    return 0;
}

static char* __ascii_to_lower_copy(const char* s)
{
    size_t len;
    char*  out;

    if (s == NULL) {
        return NULL;
    }

    len = strlen(s);
    out = malloc(len + 1);
    if (out == NULL) {
        return NULL;
    }

    for (size_t i = 0; i < len; i++) {
        out[i] = (char)tolower((unsigned char)s[i]);
    }
    out[len] = '\0';
    return out;
}

static char* __ascii_to_upper_copy(const char* s)
{
    size_t len;
    char*  out;

    if (s == NULL) {
        return NULL;
    }

    len = strlen(s);
    out = malloc(len + 1);
    if (out == NULL) {
        return NULL;
    }

    for (size_t i = 0; i < len; i++) {
        out[i] = (char)toupper((unsigned char)s[i]);
    }
    out[len] = '\0';
    return out;
}

static const char* g_windows_search_paths_64[] = {
    "/Windows/System32",
    "/Windows/System32/downlevel",
    "/Windows/SysWOW64",
    "/Windows",
    NULL
};

static const char* g_windows_search_paths_32[] = {
    "/Windows/SysWOW64",
    "/Windows/System32",
    "/Windows/System32/downlevel",
    "/Windows",
    NULL
};

static const char** __windows_paths_for_arch(enum bake_resolve_arch arch)
{
    VLOG_DEBUG("resolver", "__windows_paths_for_arch(arch=%d)\n", (int)arch);
    if (arch == BAKE_RESOLVE_ARCH_X86) {
        return g_windows_search_paths_32;
    }
    return g_windows_search_paths_64;
}

static int __try_stat(const char* path)
{
    VLOG_DEBUG("resolver", "__try_stat(path=%s)\n", path ? path : "(null)");
    struct platform_stat st;
    return platform_stat(path, &st) == 0 ? 0 : -1;
}

static const char* __try_resolve_in_dir(const char* sysroot, const char* dir, const char* dependency)
{
    char* path;
    VLOG_DEBUG("resolver", "__try_resolve_in_dir(sysroot=%s, dir=%s, dep=%s)\n",
        sysroot ? sysroot : "",
        dir ? dir : "(null)",
        dependency ? dependency : "(null)"
    );

    path = malloc(PATH_MAX);
    if (path == NULL) {
        errno = ENOMEM;
        return NULL;
    }

    snprintf(path, PATH_MAX, "%s%s/%s", sysroot ? sysroot : "", dir, dependency);
    if (__try_stat(path) == 0) {
        return path;
    }

    free(path);
    return NULL;
}

const char* resolve_platform_dependency_windows(const char* sysroot, struct bake_resolve* resolve, const char* dependency)
{
    const char** paths;
    char*        dep_lower = NULL;
    char*        dep_upper = NULL;
    const char*  resolved  = NULL;
    VLOG_DEBUG("resolver", "resolve_platform_dependency_windows(sysroot=%s, dep=%s)\n",
        sysroot ? sysroot : "",
        dependency ? dependency : "(null)"
    );

    if (dependency == NULL || resolve == NULL) {
        errno = EINVAL;
        return NULL;
    }

    paths = __windows_paths_for_arch(resolve->arch);

    // Common case-insensitivity: try original, lower, upper
    dep_lower = __ascii_to_lower_copy(dependency);
    dep_upper = __ascii_to_upper_copy(dependency);

    for (int i = 0; paths[i] != NULL; i++) {
        resolved = __try_resolve_in_dir(sysroot, paths[i], dependency);
        if (resolved) {
            break;
        }

        if (dep_lower && strcmp(dep_lower, dependency) != 0) {
            resolved = __try_resolve_in_dir(sysroot, paths[i], dep_lower);
            if (resolved) {
                break;
            }
        }

        if (dep_upper && strcmp(dep_upper, dependency) != 0) {
            resolved = __try_resolve_in_dir(sysroot, paths[i], dep_upper);
            if (resolved) {
                break;
            }
        }

        // Sometimes imports omit .dll; be helpful.
        if (!__ascii_ends_with_ignore_case(dependency, ".dll")) {
            char* with_ext;
            size_t needed = strlen(dependency) + 4 + 1;
            with_ext = malloc(needed);
            if (with_ext) {
                snprintf(with_ext, needed, "%s.dll", dependency);
                resolved = __try_resolve_in_dir(sysroot, paths[i], with_ext);
                free(with_ext);
                if (resolved) {
                    break;
                }
            }
        }
    }

    free(dep_lower);
    free(dep_upper);
    return resolved;
}

struct __system_dll {
    const char* name;
};

// Baseline DLLs expected to exist in standard Windows Server Core containers.
// This list is intentionally conservative: it focuses on OS-provided DLLs, and
// avoids ignoring app/framework-specific runtimes that may need bundling.
static const struct __system_dll g_servercore_ltsc2022_dlls[] = {
    { "ntdll.dll" },
    { "kernel32.dll" },
    { "kernelbase.dll" },
    { "user32.dll" },
    { "gdi32.dll" },
    { "gdi32full.dll" },
    { "advapi32.dll" },
    { "sechost.dll" },
    { "ws2_32.dll" },
    { "mswsock.dll" },
    { "iphlpapi.dll" },
    { "dnsapi.dll" },
    { "bcrypt.dll" },
    { "bcryptprimitives.dll" },
    { "crypt32.dll" },
    { "rpcrt4.dll" },
    { "ole32.dll" },
    { "oleaut32.dll" },
    { "combase.dll" },
    { "comctl32.dll" },
    { "shell32.dll" },
    { "shlwapi.dll" },
    { "shcore.dll" },
    { "cfgmgr32.dll" },
    { "imm32.dll" },
    { "version.dll" },
    { "psapi.dll" },
    { "userenv.dll" },
    { "sspicli.dll" },
    { "secur32.dll" },
    { "wintrust.dll" },
    { "urlmon.dll" },
    { "winmm.dll" },
    { "msvcrt.dll" },
    { "ucrtbase.dll" },
    { NULL }
};

// Nanoserver images are much slimmer; don’t over-ignore here or we’ll skip DLLs
// that need bundling when targeting nanoserver.
static const struct __system_dll g_nanoserver_ltsc2022_dlls[] = {
    { "ntdll.dll" },
    { "kernel32.dll" },
    { "kernelbase.dll" },
    { "advapi32.dll" },
    { "sechost.dll" },
    { "ws2_32.dll" },
    { "rpcrt4.dll" },
    { "bcrypt.dll" },
    { "bcryptprimitives.dll" },
    { "crypt32.dll" },
    { "msvcrt.dll" },
    { "ucrtbase.dll" },
    { NULL }
};

static const struct __system_dll* __get_system_dll_allowlist(const char* base)
{
    // base commonly looks like:
    // - "servercore:ltsc2022"
    // - "nanoserver:ltsc2022"
    // - "mcr.microsoft.com/windows/servercore:ltsc2022"
    // Defaulting to servercore is fine for typical Windows containers.
    if (base != NULL && __ascii_contains_ignore_case(base, "nanoserver")) {
        return g_nanoserver_ltsc2022_dlls;
    }
    return g_servercore_ltsc2022_dlls;
}

int resolve_is_system_library_windows(const char* base, const char* dependency)
{
    const struct __system_dll* allowlist;
    VLOG_DEBUG("resolver", "resolve_is_system_library_windows(base=%s, dep=%s)\n",
        base ? base : "(null)",
        dependency ? dependency : "(null)"
    );

    if (dependency == NULL) {
        return 0;
    }

    // Windows API set forwarders are commonly present as import names but are not
    // always real on-disk DLLs; treat them as system-provided.
    if (__ascii_starts_with_ignore_case(dependency, "api-ms-win-") ||
        __ascii_starts_with_ignore_case(dependency, "ext-ms-") ||
        __ascii_starts_with_ignore_case(dependency, "api-ms-") ||
        __ascii_starts_with_ignore_case(dependency, "ext-ms-")) {
        return 1;
    }

    allowlist = __get_system_dll_allowlist(base);
    VLOG_DEBUG("resolver", "resolve_is_system_library_windows(base=%s) using %s allowlist\n",
        base ? base : "(null)",
        (base != NULL && __ascii_contains_ignore_case(base, "nanoserver")) ? "nanoserver" : "servercore"
    );

    for (int i = 0; allowlist[i].name != NULL; i++) {
        if (__ascii_equals_ignore_case(allowlist[i].name, dependency)) {
            return 1;
        }
    }
    return 0;
}
