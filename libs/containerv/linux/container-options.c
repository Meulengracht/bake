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

#include "private.h"
#include <chef/containerv/policy.h>
#include <stdlib.h>

struct containerv_options* containerv_options_new(void)
{
    return calloc(1, sizeof(struct containerv_options));
}

void containerv_options_delete(struct containerv_options* options)
{
    if (options == NULL) {
        return;
    }

    containerv_policy_delete(options->policy);
    free(options);
}

void containerv_options_set_caps(struct containerv_options* options, enum containerv_capabilities caps)
{
    options->capabilities = caps;
}

void containerv_options_set_policy(struct containerv_options* options, struct containerv_policy* policy)
{
    if (options->policy != NULL) {
        containerv_policy_delete(options->policy);
    }
    options->policy = policy;
}

void containerv_options_set_layers(struct containerv_options* options, struct containerv_layer_context* layers)
{
    options->layers = layers;
}

void containerv_options_set_users(struct containerv_options* options, uid_t hostUidStart, uid_t childUidStart, int count)
{
    options->uid_range.host_start = hostUidStart;
    options->uid_range.child_start = childUidStart;
    options->uid_range.count = count;
}

void containerv_options_set_groups(struct containerv_options* options, gid_t hostGidStart, gid_t childGidStart, int count)
{
    options->gid_range.host_start = hostGidStart;
    options->gid_range.child_start = childGidStart;
    options->gid_range.count = count;
}

void containerv_options_set_cgroup_limits(
    struct containerv_options* options,
    const char*                memory_max,
    const char*                cpu_weight,
    const char*                pids_max)
{
    options->cgroup.memory_max = memory_max;
    options->cgroup.cpu_weight = cpu_weight;
    options->cgroup.pids_max = pids_max;
}

void containerv_options_set_network(
    struct containerv_options* options,
    const char*                container_ip,
    const char*                container_netmask,
    const char*                host_ip)
{
    containerv_options_set_network_ex(options, container_ip, container_netmask, host_ip, NULL, NULL);
}

void containerv_options_set_windows_wcow_parent_layers(
    struct containerv_options* options,
    const char* const*         parent_layers,
    int                        parent_layer_count)
{
    (void)options;
    (void)parent_layers;
    (void)parent_layer_count;
}

void containerv_options_set_network_ex(
    struct containerv_options* options,
    const char*                container_ip,
    const char*                container_netmask,
    const char*                host_ip,
    const char*                gateway_ip,
    const char*                dns)
{
    if (options == NULL) {
        return;
    }

    options->network.enable = 1;
    options->network.container_ip = container_ip;
    options->network.container_netmask = container_netmask;
    options->network.host_ip = host_ip;
    options->network.gateway_ip = gateway_ip;
    options->network.dns = dns;
}
