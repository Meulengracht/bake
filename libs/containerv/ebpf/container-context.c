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

#define _GNU_SOURCE

#include <chef/platform.h>
#include <protecc/protecc.h>

#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vlog.h>

#include "container-context.h"
#include "map-ops.h"

// import the private.h from the policies dir
#include "../policies/private.h"

struct bpf_container_context* bpf_container_context_new(
    const char*        containerId,
    unsigned long long cgroupId)
{
    struct bpf_container_context* context;
    
    context = calloc(1, sizeof(struct bpf_container_context));
    if (context == NULL) {
        return NULL;
    }
    
    context->container_id = strdup(containerId);
    if (context->container_id == NULL) {
        free(context);
        return NULL;
    }
    
    // Set initial members and capacities
    context->cgroup_id = cgroupId;
    context->net.create_key_capacity = 16;
    context->net.tuple_key_capacity = 32;
    context->net.unix_key_capacity = 16;

    context->net.create_keys = malloc(sizeof(struct bpf_net_create_key) * context->net.create_key_capacity);
    context->net.tuple_keys = malloc(sizeof(struct bpf_net_tuple_key) * context->net.tuple_key_capacity);
    context->net.unix_keys = malloc(sizeof(struct bpf_net_unix_key) * context->net.unix_key_capacity);

    if (context->net.create_keys == NULL||
        context->net.tuple_keys == NULL|| context->net.unix_keys == NULL) {
        bpf_container_context_delete(context);
        return NULL;
    }
    return context;
}

void bpf_container_context_delete(struct bpf_container_context* context)
{
    if (context == NULL) {
        return;
    }

    free(context->net.tuple_keys);
    free(context->net.create_keys);
    free(context->net.unix_keys);
    free(context->container_id);
    free(context);
}

static int __ensure_capacity_sized(void** keys, int* count, int* capacity, size_t elem_size)
{
    if (*count < *capacity) {
        return 0;
    }
    if (*capacity >= MAX_TRACKED_ENTRIES) {
        return -1;
    }
    int new_capacity = (*capacity * 2 < MAX_TRACKED_ENTRIES) ? (*capacity * 2) : MAX_TRACKED_ENTRIES;
    void* new_keys = realloc(*keys, elem_size * (size_t)new_capacity);
    if (!new_keys) {
        return -1;
    }
    *keys = new_keys;
    *capacity = new_capacity;
    return 0;
}

int bpf_container_context_add_tracked_net_create_entry(
    struct bpf_container_context*    context,
    const struct bpf_net_create_key* key)
{
    if (!context || !key) {
        return -1;
    }

    if (__ensure_capacity_sized((void**)&context->net.create_keys,
                                &context->net.create_key_count,
                                &context->net.create_key_capacity,
                                sizeof(*context->net.create_keys)) < 0) {
        VLOG_WARNING("cvd", "bpf_manager: failed to expand tracked net create key capacity\n");
        return -1;
    }

    context->net.create_keys[context->net.create_key_count++] = *key;
    return 0;
}

int bpf_container_context_add_tracked_net_tuple_entry(
    struct bpf_container_context* context,
    const struct bpf_net_tuple_key* key)
{
    if (!context || !key) {
        return -1;
    }

    if (__ensure_capacity_sized((void**)&context->net.tuple_keys,
                                &context->net.tuple_key_count,
                                &context->net.tuple_key_capacity,
                                sizeof(*context->net.tuple_keys)) < 0) {
        VLOG_WARNING("cvd", "bpf_manager: failed to expand tracked net tuple key capacity\n");
        return -1;
    }

    context->net.tuple_keys[context->net.tuple_key_count++] = *key;
    return 0;
}

int bpf_container_context_add_tracked_net_unix_entry(
    struct bpf_container_context* context,
    const struct bpf_net_unix_key* key)
{
    if (!context || !key) {
        return -1;
    }

    if (__ensure_capacity_sized((void**)&context->net.unix_keys,
                                &context->net.unix_key_count,
                                &context->net.unix_key_capacity,
                                sizeof(*context->net.unix_keys)) < 0) {
        VLOG_WARNING("cvd", "bpf_manager: failed to expand tracked net unix key capacity\n");
        return -1;
    }

    context->net.unix_keys[context->net.unix_key_count++] = *key;
    return 0;
}

void bpf_container_context_apply_paths(
    struct bpf_container_context* containerContext,
    struct containerv_policy*     policy,
    struct bpf_map_context*       mapContext,
    const char*                   rootfsPath)
{
    protecc_compiled_t* profile = NULL;
    protecc_error_t     err;
    protecc_compile_config_t compileConfig;
    protecc_pattern_t*  paths = NULL;
    void*               binaryProfile = NULL;
    size_t              binaryProfileSize;

    paths = calloc(policy->path_count, sizeof(protecc_pattern_t));
    if (paths == NULL) {
        VLOG_WARNING("cvd", "bpf_manager: failed to allocate paths array for protecc compilation\n");
        return;
    }

    for (int i = 0; i < policy->path_count; i++) {
        const struct containerv_policy_path* p = &policy->paths[i];
        paths[i].pattern = strpathcombine(rootfsPath, p->path);
        paths[i].perms = p->access;
    }

    protecc_compile_config_default(&compileConfig);
    compileConfig.mode = PROTECC_COMPILE_MODE_DFA;

    err = protecc_compile(paths, policy->path_count, PROTECC_FLAG_OPTIMIZE, &compileConfig, &profile);
    if (err != PROTECC_OK) {
        VLOG_WARNING("cvd", "bpf_manager: failed to compile protecc patterns for container %s: %s\n",
                     containerContext->container_id, protecc_error_string(err));
        goto cleanup;
    }
    
    err = protecc_export(profile, NULL, 0, &binaryProfileSize);
    if (err != PROTECC_OK) {
        VLOG_WARNING("cvd", "bpf_manager: unexpected error querying protecc export size for container %s: %s\n",
                     containerContext->container_id, protecc_error_string(err));
        goto cleanup;
    }

    binaryProfile = malloc(binaryProfileSize);
    if (binaryProfile == NULL) {
        VLOG_WARNING("cvd", "bpf_manager: failed to allocate buffer for protecc export for container %s\n", containerContext->container_id);
        goto cleanup;
    }

    err = protecc_export(profile, binaryProfile, binaryProfileSize, &binaryProfileSize);
    if (err != PROTECC_OK) {
        VLOG_WARNING("cvd", "bpf_manager: failed to export protecc profile for container %s: %s\n",
                     containerContext->container_id, protecc_error_string(err));
        goto cleanup;
    }

    if (bpf_profile_map_set_profile(mapContext, binaryProfile, binaryProfileSize)) {
        VLOG_WARNING("cvd", "bpf_manager: failed to set profile map for container %s: %s\n",
                     containerContext->container_id, strerror(errno));
    }

cleanup:
    free(binaryProfile);
    protecc_free(profile);
    if (paths != NULL) {
         for (int i = 0; i < policy->path_count; i++) {
            free((void*)paths[i].pattern);
        }
        free(paths);
    }
}

void bpf_container_context_apply_net(
    struct bpf_container_context* containerContext,
    struct containerv_policy*     policy,
    struct bpf_map_context*       mapContext,
    const char*                   rootfsPath)
{
    for (int i = 0; i < policy->net_rule_count; i++) {
        const struct containerv_policy_net_rule* rule = &policy->net_rules[i];
        unsigned int create_mask = rule->allow_mask & BPF_NET_CREATE;
        unsigned int tuple_mask = rule->allow_mask & ~BPF_NET_CREATE;

        if (create_mask) {
            struct bpf_net_create_key ckey = {
                .cgroup_id = containerContext->cgroup_id,
                .family = (unsigned int)rule->family,
                .type = (unsigned int)rule->type,
                .protocol = (unsigned int)rule->protocol,
            };
            if (bpf_net_create_map_allow(mapContext, &ckey, create_mask) == 0) {
                (void)bpf_container_context_add_tracked_net_create_entry(containerContext, &ckey);
            } else {
                VLOG_WARNING("cvd", "bpf_manager: failed to apply net create rule (family=%d type=%d proto=%d): %s\n",
                                rule->family, rule->type, rule->protocol, strerror(errno));
            }
        }

        if (tuple_mask == 0) {
            continue;
        }

        if (rule->family == AF_UNIX) {
            struct bpf_net_unix_key ukey = {0};
            if (rule->unix_path == NULL || rule->unix_path[0] == '\0') {
                VLOG_WARNING("cvd", "bpf_manager: net unix rule missing path (family=AF_UNIX)\n");
                continue;
            }
            ukey.cgroup_id = containerContext->cgroup_id;
            ukey.type = (unsigned int)rule->type;
            ukey.protocol = (unsigned int)rule->protocol;

            if (rule->unix_path[0] == '@') {
                size_t path_len = strlen(rule->unix_path + 1);
                size_t max_len = BPF_NET_UNIX_PATH_MAX - 1;
                if (path_len == 0) {
                    VLOG_WARNING("cvd", "bpf_manager: net unix rule missing abstract name (family=AF_UNIX)\n");
                    continue;
                }
                if (path_len > max_len) {
                    VLOG_WARNING("cvd", "bpf_manager: net unix abstract path too long (%zu)\n", path_len);
                    continue;
                }
                ukey.is_abstract = 1;
                ukey.path_len = (unsigned int)path_len;
                memcpy(ukey.path, rule->unix_path + 1, path_len);
            } else {
                size_t path_len = strlen(rule->unix_path);
                if (path_len >= BPF_NET_UNIX_PATH_MAX) {
                    VLOG_WARNING("cvd", "bpf_manager: net unix path too long (%zu)\n", path_len);
                    continue;
                }
                ukey.is_abstract = 0;
                ukey.path_len = (unsigned int)path_len;
                memcpy(ukey.path, rule->unix_path, path_len);
                ukey.path[path_len] = '\0';
            }

            if (bpf_net_unix_map_allow(mapContext, &ukey, tuple_mask) == 0) {
                (void)bpf_container_context_add_tracked_net_unix_entry(containerContext, &ukey);
            } else {
                VLOG_WARNING("cvd", "bpf_manager: failed to apply net unix rule (%s): %s\n",
                                rule->unix_path, strerror(errno));
            }
            continue;
        }

        if (rule->addr_len > BPF_NET_ADDR_MAX) {
            VLOG_WARNING("cvd", "bpf_manager: net rule addr_len too large (%u)\n", rule->addr_len);
            continue;
        }

        struct bpf_net_tuple_key tkey = {0};
        tkey.cgroup_id = containerContext->cgroup_id;
        tkey.family = (unsigned int)rule->family;
        tkey.type = (unsigned int)rule->type;
        tkey.protocol = (unsigned int)rule->protocol;
        tkey.port = rule->port;
        if (rule->addr_len > 0) {
            memcpy(tkey.addr, rule->addr, rule->addr_len);
        }

        if (bpf_net_tuple_map_allow(mapContext, &tkey, tuple_mask) == 0) {
            (void)bpf_container_context_add_tracked_net_tuple_entry(containerContext, &tkey);
        } else {
            VLOG_WARNING("cvd", "bpf_manager: failed to apply net tuple rule (family=%d type=%d proto=%d): %s\n",
                            rule->family, rule->type, rule->protocol, strerror(errno));
        }
    }
}

int bpf_container_context_cleanup(
    struct bpf_container_context* containerContext,
    struct bpf_map_context*       mapContext)
{    
    if (mapContext->profile_map_fd >= 0) {
        int deleted_count = bpf_profile_map_clear_profile(mapContext);
        if (deleted_count) {
            VLOG_ERROR("cvd", "bpf_manager: batch deletion failed (file map) for container %s\n", containerContext->container_id);
            return -1;
        }
    }

    if (containerContext->net.create_key_count > 0 && mapContext->net_create_map_fd >= 0) {
        VLOG_DEBUG("cvd", "bpf_manager: deleting %d net create entries (cgroup_id=%llu)\n",
                   containerContext->net.create_key_count, containerContext->cgroup_id);
        int deleted_net = bpf_map_delete_batch_by_fd(
            mapContext->net_create_map_fd,
            containerContext->net.create_keys,
            containerContext->net.create_key_count,
            sizeof(struct bpf_net_create_key)
        );
        if (deleted_net < 0) {
            VLOG_ERROR("cvd", "bpf_manager: batch deletion failed (net create map) for container %s\n", containerContext->container_id);
            return -1;
        }
    }

    if (containerContext->net.tuple_key_count > 0 && mapContext->net_tuple_map_fd >= 0) {
        VLOG_DEBUG("cvd", "bpf_manager: deleting %d net tuple entries (cgroup_id=%llu)\n",
                   containerContext->net.tuple_key_count, containerContext->cgroup_id);
        int deleted_net = bpf_map_delete_batch_by_fd(
            mapContext->net_tuple_map_fd,
            containerContext->net.tuple_keys,
            containerContext->net.tuple_key_count,
            sizeof(struct bpf_net_tuple_key)
        );
        if (deleted_net < 0) {
            VLOG_ERROR("cvd", "bpf_manager: batch deletion failed (net tuple map) for container %s\n", containerContext->container_id);
            return -1;
        }
    }

    if (containerContext->net.unix_key_count > 0 && mapContext->net_unix_map_fd >= 0) {
        VLOG_DEBUG("cvd", "bpf_manager: deleting %d net unix entries (cgroup_id=%llu)\n",
                   containerContext->net.unix_key_count, containerContext->cgroup_id);
        int deleted_net = bpf_map_delete_batch_by_fd(
            mapContext->net_unix_map_fd,
            containerContext->net.unix_keys,
            containerContext->net.unix_key_count,
            sizeof(struct bpf_net_unix_key)
        );
        if (deleted_net < 0) {
            VLOG_ERROR("cvd", "bpf_manager: batch deletion failed (net unix map) for container %s\n", containerContext->container_id);
            return -1;
        }
    }
    return 0;
}
