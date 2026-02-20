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
    
    return context;
}

void bpf_container_context_delete(struct bpf_container_context* context)
{
    if (context == NULL) {
        return;
    }

    free(context->container_id);
    free(context);
}

void bpf_container_context_apply_paths(
    struct bpf_container_context* containerContext,
    struct containerv_policy*     policy,
    struct bpf_map_context*       mapContext,
    const char*                   rootfsPath)
{
    protecc_compile_config_t compileConfig;
    protecc_profile_t*  profile = NULL;
    protecc_error_t     err;
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

    err = protecc_compile_patterns(paths, policy->path_count, PROTECC_FLAG_OPTIMIZE, &compileConfig, &profile);
    if (err != PROTECC_OK) {
        VLOG_WARNING("cvd", "bpf_manager: failed to compile protecc patterns for container %s: %s\n",
                     containerContext->container_id, protecc_error_string(err));
        goto cleanup;
    }
    
    err = protecc_profile_export_path(profile, NULL, 0, &binaryProfileSize);
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

    err = protecc_profile_export_path(profile, binaryProfile, binaryProfileSize, &binaryProfileSize);
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
    protecc_compile_config_t config;
    protecc_profile_builder_t* builder;
    protecc_profile_t*  profile = NULL;
    protecc_error_t     err;
    void*               binaryProfile = NULL;
    size_t              binaryProfileSize;

    builder = protecc_profile_builder_create();
    if (builder == NULL) {
        VLOG_WARNING("cvd", "bpf_manager: failed to create protecc profile builder for container %s\n", containerContext->container_id);
        return;
    }

    for (int i = 0; i < policy->net_rule_count; i++) {
        const struct containerv_policy_net_rule* rule = &policy->net_rules[i];

        err = protecc_profile_builder_add_net_rule(builder, &(protecc_net_rule_t){
            .action = rule->allow_mask ? PROTECC_ACTION_ALLOW : PROTECC_ACTION_DENY,
            .family = rule->family,
            .protocol = rule->protocol,
            .port_from = rule->port,
            .port_to = rule->port,
            .ip_pattern = NULL, // Not used in builder
            .unix_path_pattern = rule->unix_path ? strpathcombine(rootfsPath, rule->unix_path) : NULL,
        });
        if (err != PROTECC_OK) {
            goto cleanup;
        }
    }

    protecc_compile_config_default(&config);
    config.mode = PROTECC_COMPILE_MODE_DFA;

    err = protecc_profile_compile(builder, 0, &config, &profile);
    if (err != PROTECC_OK) {
        VLOG_WARNING("cvd", "bpf_manager: failed to compile protecc net rules for container %s: %s\n",
                     containerContext->container_id, protecc_error_string(err));
        goto cleanup;
    }
    
    err = protecc_profile_export_net(profile, NULL, 0, &binaryProfileSize);
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

    err = protecc_profile_export_net(profile, binaryProfile, binaryProfileSize, &binaryProfileSize);
    if (err != PROTECC_OK) {
        VLOG_WARNING("cvd", "bpf_manager: failed to export protecc profile for container %s: %s\n",
                     containerContext->container_id, protecc_error_string(err));
        goto cleanup;
    }

    if (bpf_profile_map_set_net_profile(mapContext, binaryProfile, binaryProfileSize)) {
        VLOG_WARNING("cvd", "bpf_manager: failed to set net profile map for container %s: %s\n",
                     containerContext->container_id, strerror(errno));
    }

cleanup:
    protecc_profile_builder_destroy(builder);
    free(binaryProfile);
    protecc_free(profile);
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

    if (mapContext->net_profile_map_fd >= 0) {
        int deleted_count = bpf_profile_map_clear_net_profile(mapContext);
        if (deleted_count) {
            VLOG_ERROR("cvd", "bpf_manager: batch deletion failed (net map) for container %s\n", containerContext->container_id);
            return -1;
        }
    }

    if (mapContext->mount_profile_map_fd >= 0) {
        int deleted_count = bpf_profile_map_clear_mount_profile(mapContext);
        if (deleted_count) {
            VLOG_ERROR("cvd", "bpf_manager: batch deletion failed (mount map) for container %s\n", containerContext->container_id);
            return -1;
        }
    }
    return 0;
}
