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
#include <stdlib.h>
#include <string.h>
#include <vlog.h>

#include "../private.h"

// Add default system paths (always needed for basic functionality)
static const struct containerv_policy_path g_basePolicyPaths[] = {
    { "/lib", CV_FS_READ | CV_FS_EXEC },
    { "/lib64", CV_FS_READ | CV_FS_EXEC },
    { "/usr/lib", CV_FS_READ | CV_FS_EXEC },
    { "/bin", CV_FS_READ | CV_FS_EXEC },
    { "/usr/bin", CV_FS_READ | CV_FS_EXEC },
    { "/dev/null", CV_FS_READ },
    { "/dev/zero", CV_FS_READ },
    { "/dev/urandom", CV_FS_READ },
    { "/dev/random", CV_FS_READ },
    { "/dev/tty", CV_FS_READ | CV_FS_WRITE },
    { "/etc/ld.so.cache", CV_FS_READ },  // Dynamic linker cache
    { "/etc/ld.so.conf", CV_FS_READ },   // Dynamic linker config
    { "/etc/ld.so.conf.d", CV_FS_READ }, // Dynamic linker config directory
    { "/proc/self", CV_FS_READ }, // Process self information
    { "/sys/devices/system/cpu", CV_FS_READ }, // CPU information (for runtime optimization)
    { NULL, 0 }
};

static const struct containerv_policy_path g_buildPolicyPaths[] = {
    { "/usr/include", CV_FS_READ | CV_FS_EXEC },
    { "/usr/share/pkgconfig", CV_FS_READ | CV_FS_EXEC },
    { "/usr/lib/pkgconfig", CV_FS_READ | CV_FS_EXEC },
    { NULL, 0 }
};

static const struct containerv_policy_path g_networkPolicyPaths[] = {
    { "/etc/ssl", CV_FS_READ | CV_FS_EXEC },
    { "/etc/ca-certificates", CV_FS_READ | CV_FS_EXEC },
    { "/etc/resolv.conf", CV_FS_READ | CV_FS_EXEC },
    { "/etc/hosts", CV_FS_READ | CV_FS_EXEC },
    { NULL, 0 }
};

static int __policy_add_path(struct containerv_policy* policy, const char* path, enum containerv_fs_access access)
{
    if (policy == NULL || path == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (policy->path_count >= MAX_PATHS) {
        VLOG_ERROR("containerv", "policy_ebpf: too many paths\n");
        errno = ENOMEM;
        return -1;
    }

    policy->paths[policy->path_count].path = strdup(path);
    if (policy->paths[policy->path_count].path == NULL) {
        return -1;
    }
    policy->paths[policy->path_count].access = access;
    policy->path_count++;
    return 0;
}

static int __policy_add_paths(struct containerv_policy* policy, const struct containerv_policy_path* paths)
{
    for (size_t i = 0; paths[i].path != NULL; i++) {
        if (__policy_add_path(policy, paths[i].path, paths[i].access) != 0) {
            VLOG_ERROR("containerv", "policy_ebpf: failed to add path '%s'\n", paths[i].path);
            return -1;
        }
    }
    return 0;
}

int policy_ebpf_build(struct containerv_policy* policy, struct containerv_policy_plugin* plugin)
{
    if (policy == NULL || plugin == NULL) {
        errno = EINVAL;
        return -1;
    }
    
    if (strcmp(plugin->name, "minimal") == 0) {
        return __policy_add_paths(policy, &g_basePolicyPaths[0]);
    } else if (strcmp(plugin->name, "build") == 0) {
        return __policy_add_paths(policy, &g_buildPolicyPaths[0]);
    } else if (strcmp(plugin->name, "network") == 0) {
        return __policy_add_paths(policy, &g_networkPolicyPaths[0]);
    } else {
        VLOG_ERROR("containerv", "policy_ebpf: unknown plugin '%s'\n", plugin->name);
        errno = EINVAL;
        return -1;
    }
}
