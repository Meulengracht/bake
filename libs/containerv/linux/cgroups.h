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

#ifndef __CONTAINERV_CGROUPS_H__
#define __CONTAINERV_CGROUPS_H__

#include <sys/types.h>

struct containerv_cgroup_limits {
    const char* memory_max;      // e.g., "1G", "512M", or "max" for no limit
    const char* cpu_weight;      // 1-10000, default is 100
    const char* pids_max;        // maximum number of processes, or "max"
    int         enable_devices;  // whether to enable device control
};

/**
 * @brief Initialize cgroups for a container process
 * @param hostname The hostname/name for the cgroup
 * @param pid The process ID to add to the cgroup
 * @param limits The resource limits to apply, or NULL for defaults
 * @return 0 on success, -1 on failure
 */
extern int cgroups_init(const char* hostname, pid_t pid, const struct containerv_cgroup_limits* limits);

/**
 * @brief Clean up cgroups for a container
 * @param hostname The hostname/name of the cgroup to remove
 * @return 0 on success, -1 on failure
 */
extern int cgroups_free(const char* hostname);

#endif //!__CONTAINERV_CGROUPS_H__
