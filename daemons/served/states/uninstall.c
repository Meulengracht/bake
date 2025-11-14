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

#include <transaction/states/uninstall.h>
#include <transaction/transaction.h>
#include <state.h>
#include <utils.h>

#include <chef/platform.h>
#include <vlog.h>
#include <stdlib.h>

enum sm_action_result served_handle_state_uninstall(void* context)
{
    struct served_transaction* transaction = context;
    struct state_transaction*  state;
    struct state_application*  application;
    sm_event_t                 event = SERVED_TX_EVENT_FAILED;
    char*                      storagePath = NULL;
    char**                     names = NULL;
    int                        status;

    served_state_lock();
    state = served_state_transaction(transaction->id);
    if (state == NULL) {
        served_state_unlock();
        served_sm_post_event(&transaction->sm, SERVED_TX_EVENT_FAILED);
        return SM_ACTION_CONTINUE;
    }
    
    // Get the application to uninstall
    application = served_state_application(state->name);
    if (application == NULL) {
        VLOG_ERROR("served", "Application %s not found in state\n", state->name);
        served_state_unlock();
        served_sm_post_event(&transaction->sm, SERVED_TX_EVENT_FAILED);
        return SM_ACTION_CONTINUE;
    }
    served_state_unlock();

    // Split package name to get publisher/package components
    names = utils_split_package_name(state->name);
    if (names == NULL) {
        VLOG_ERROR("served", "Failed to split package name %s\n", state->name);
        goto cleanup;
    }

    // Build the storage path for the package file
    storagePath = utils_path_pack(names[0], names[1]);
    if (storagePath == NULL) {
        VLOG_ERROR("served", "Failed to build storage path for %s\n", state->name);
        goto cleanup;
    }

    served_state_lock();
    status = served_state_remove_application(application);
    if (status) {
        VLOG_ERROR("served", "Failed to remove application %s from state: %d\n", state->name, status);
        served_state_unlock();
        goto cleanup;
    }
    served_state_unlock();
    
    status = platform_unlink(storagePath);
    if (status) {
        VLOG_ERROR("served", "Failed to remove package file %s: %d\n", storagePath, status);
        served_state_unlock();
        goto cleanup;
    }
    
    VLOG_DEBUG("served", "Successfully uninstalled package %s\n", state->name);
    event = SERVED_TX_EVENT_OK;

cleanup:
    strsplit_free(names);
    free((void*)storagePath);
    served_sm_post_event(&transaction->sm, event);
    return SM_ACTION_CONTINUE;
}
