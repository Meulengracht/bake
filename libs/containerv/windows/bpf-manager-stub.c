/**
 * Copyright, Philip Meulengracht
 *
 * This program is free software : you can redistribute it and / or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
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

#include <chef/containerv/bpf-manager.h>
#include <vlog.h>

int containerv_bpf_manager_initialize(void)
{
    VLOG_WARNING("containerv[bpf]", "BPF manager is not available on Windows\n");
    return 0;
}

void containerv_bpf_manager_shutdown(void)
{
    // no-op on Windows
}

int containerv_bpf_manager_is_available(void)
{
    return 0;
}

int containerv_bpf_manager_get_policy_map_fd(void)
{
    return -1;
}

int containerv_bpf_manager_populate_policy(
    const char* container_id,
    const char* rootfs_path,
    struct containerv_policy* policy)
{
    (void)container_id;
    (void)rootfs_path;
    (void)policy;
    return -1;
}

int containerv_bpf_manager_cleanup_policy(const char* container_id)
{
    (void)container_id;
    return -1;
}

int containerv_bpf_manager_get_metrics(struct containerv_bpf_metrics* metrics)
{
    if (metrics) {
        metrics->available = 0;
        metrics->total_containers = 0;
        metrics->total_policy_entries = 0;
        metrics->max_map_capacity = 0;
        metrics->total_populate_ops = 0;
        metrics->total_cleanup_ops = 0;
        metrics->failed_populate_ops = 0;
        metrics->failed_cleanup_ops = 0;
    }
    return -1;
}

int containerv_bpf_manager_get_container_metrics(
    const char* container_id,
    struct containerv_bpf_container_metrics* metrics)
{
    (void)container_id;
    if (metrics) {
        metrics->container_id[0] = '\0';
        metrics->cgroup_id = 0;
        metrics->policy_entry_count = 0;
        metrics->populate_time_us = 0;
        metrics->cleanup_time_us = 0;
    }
    return -1;
}

int containerv_bpf_manager_sanity_check_pins(void)
{
    return 0;
}
