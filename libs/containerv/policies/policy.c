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
