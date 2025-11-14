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

#include <transaction/states/unload.h>
#include <transaction/transaction.h>
#include <state.h>
#include <utils.h>

#include <chef/platform.h>
#include <vlog.h>

static int __unload_application(const char* name)
{
    char** names = NULL;
    char   containerId[256];
    int    status;
    
    names = utils_split_package_name(name);
    if (names == NULL) {
        return -1;
    }

    snprintf(&containerId[0], sizeof(containerId), "%s.%s", names[0], names[1]);
    strsplit_free(names);

    status = container_client_destroy_container(&containerId[0]);
    if (status) {
        VLOG_ERROR("served", "failed to destroy container for package %s\n",
            name
        );
    }
    return status;
}

enum sm_action_result served_handle_state_unload(void* context)
{
    struct served_transaction* transaction = context;
    int                        status;

    // Format name in the form publisher.package
    VLOG_DEBUG("served", "Unloading container for package %s\n",
        transaction->name
    );

    // Destroy the container running for publisher.package
    status = __unload_application(transaction->name);
    if (status) {
        served_sm_post_event(&transaction->sm, SERVED_TX_EVENT_FAILED);
        return SM_ACTION_CONTINUE;
    }

    served_sm_post_event(&transaction->sm, SERVED_TX_EVENT_OK);
    return SM_ACTION_CONTINUE;
}

enum sm_action_result served_handle_state_unload_all(void* context)
{
    struct served_transaction* transaction = context;
    struct state_application*  applications;
    int                        count;
    int                        status;
    sm_event_t                 event = SERVED_TX_EVENT_FAILED;
    
    served_state_lock();
    status = served_state_get_applications(&applications, &count);
    if (status) {
        goto cleanup;
    }

    for (int i = 0; i < count; i++) {
        struct state_application* app = &applications[i];
        status = __unload_application(app->name);
        if (status) {
            VLOG_ERROR("served", "Failed to unload application %s: %d\n", app->name, status);
            // continue
        }
    }

    event = SERVED_TX_EVENT_OK;

cleanup:
    served_state_unlock();
    served_sm_post_event(&transaction->sm, event);
    return SM_ACTION_CONTINUE;
}
