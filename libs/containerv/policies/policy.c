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

#include "private.h"

struct containerv_policy* containerv_policy_new(struct list* plugins)
{
    struct containerv_policy* policy;
    struct list_item*         i;
    
    policy = calloc(1, sizeof(struct containerv_policy));
    if (policy == NULL) {
        return NULL;
    }

    list_foreach(plugins, i) {
        struct containerv_policy_plugin* plugin = (struct containerv_policy_plugin*)i;
        
        // Invoker all handlers with the plugin
        for (int j = 0; g_policy_handlers[j].name != NULL; j++) {
            if (g_policy_handlers[j].apply(policy, plugin)) {
                VLOG_ERROR("containerv", "policy: handler for plugin type '%s' failed\n", plugin->name);
                goto error;
            }
        }
    }
    
    return policy;
    
error:
    containerv_policy_delete(policy);
    return NULL;
}

void containerv_policy_delete(struct containerv_policy* policy)
{
    if (policy == NULL) {
        return;
    }
    
    // Free syscall names
    for (int i = 0; i < policy->syscall_count; i++) {
        free(policy->syscalls[i].name);
    }
    
    // Free path strings
    for (int i = 0; i < policy->path_count; i++) {
        free(policy->paths[i].path);
    }
        
    free(policy);
}

static void __free_policy_plugin(void* item)
{
    free(item);
}

struct containerv_policy* containerv_policy_from_strings(const char* profiles)
{
    struct list plugins;
    struct containerv_policy* policy;
    int want_build = 0;
    int want_network = 0;

    list_init(&plugins);

    // Always include minimal base policy.
    {
        struct containerv_policy_plugin* plugin = calloc(1, sizeof(*plugin));
        if (!plugin) {
            return NULL;
        }
        plugin->name = "minimal";
        list_add(&plugins, &plugin->header);
    }

    // Parse optional profiles: "build", "network" (comma-separated)
    if (profiles != NULL && profiles[0] != '\0') {
        char* copy = strdup(profiles);
        if (!copy) {
            list_destroy(&plugins, __free_policy_plugin);
            return NULL;
        }

        char* saveptr = NULL;
        for (char* tok = strtok_r(copy, ",", &saveptr); tok != NULL; tok = strtok_r(NULL, ",", &saveptr)) {
            while (*tok == ' ' || *tok == '\t' || *tok == '\n' || *tok == '\r') {
                tok++;
            }
            char* end = tok + strlen(tok);
            while (end > tok && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\n' || end[-1] == '\r')) {
                end[-1] = '\0';
                end--;
            }

            if (tok[0] == '\0') {
                continue;
            }
            if (strcmp(tok, "build") == 0) {
                want_build = 1;
            } else if (strcmp(tok, "network") == 0) {
                want_network = 1;
            } else {
                VLOG_WARNING("cvd", "cvd_create: unknown policy profile '%s' (ignoring)\n", tok);
            }
        }
        free(copy);
    }

    if (want_build) {
        struct containerv_policy_plugin* plugin = calloc(1, sizeof(*plugin));
        if (!plugin) {
            list_destroy(&plugins, __free_policy_plugin);
            return NULL;
        }
        plugin->name = "build";
        list_add(&plugins, &plugin->header);
    }

    if (want_network) {
        struct containerv_policy_plugin* plugin = calloc(1, sizeof(*plugin));
        if (!plugin) {
            list_destroy(&plugins, __free_policy_plugin);
            return NULL;
        }
        plugin->name = "network";
        list_add(&plugins, &plugin->header);
    }

    policy = containerv_policy_new(&plugins);
    list_destroy(&plugins, __free_policy_plugin);
    return policy;
}
