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

#include <chef/platform.h>
#include <errno.h>
#include <gracht/server.h>
#include <transaction/states/verify.h>
#include <transaction/states/types.h>
#include <transaction/transaction.h>
#include <transaction/logging.h>
#include <state.h>
#include <utils.h>
#include <vlog.h>

// Protocol headers for event emission
#include "chef_served_service_server.h"

static void __emit_verify_progress(
    struct served_transaction* transaction,
    unsigned long long bytes_current,
    unsigned long long bytes_total)
{
    unsigned int percentage;
    gracht_server_t* server;
    
    if (bytes_total == 0) {
        return;
    }
    
    percentage = (unsigned int)((bytes_current * 100) / bytes_total);
    
    transaction->io_progress.bytes_current = bytes_current;
    transaction->io_progress.bytes_total = bytes_total;
    
    struct chef_transaction_io_progress eventInfo = {
        .id = transaction->id,
        .state = CHEF_TRANSACTION_STATE_VERIFYING,
        .bytes_current = bytes_current,
        .bytes_total = bytes_total,
        .percentage = percentage
    };
    
    server = served_gracht_server();
    if (server) {
        chef_served_event_transaction_io_progress_all(server, &eventInfo);
    }
    
    VLOG_DEBUG("served", "Verify progress: %llu/%llu (%u%%)\n",
               bytes_current, bytes_total, percentage);
}

enum sm_action_result served_handle_state_verify(void* context)
{
    struct served_transaction* transaction = context;
    struct state_transaction*  state;
    int                        status;
    char**                     names;

    const char* name;
    int revision;

    // Reset progress tracking
    transaction->io_progress.bytes_current = 0;
    transaction->io_progress.bytes_total = 0;
    transaction->io_progress.last_reported_percentage = 0;

    served_state_lock();
    state = served_state_transaction(transaction->id);
    if (state == NULL) {
        served_state_unlock();
        served_sm_post_event(&transaction->sm, SERVED_TX_EVENT_FAILED);
        return SM_ACTION_CONTINUE;
    }

    name = state->name;
    revision = state->revision;
    served_state_unlock();
    
    names = utils_split_package_name(name);
    if (names == NULL) {
        served_transaction_log_error(transaction,
            "Invalid package name format (must be 'publisher/package')");
        served_sm_post_event(&transaction->sm, SERVED_TX_EVENT_FAILED);
        return SM_ACTION_CONTINUE;
    }

    // Emit progress events (start and end for now)
    __emit_verify_progress(transaction, 0, 100);
    
    status = utils_verify_package(names[0], names[1], revision);
    
    if (status == 0) {
        __emit_verify_progress(transaction, 100, 100);
        TXLOG_INFO(transaction, "Package verification successful");
    } else {
        // Log error details
        if (errno == ENOENT) {
            TXLOG_ERROR(transaction, "Package file not found for verification");
        } else if (errno) {
            TXLOG_ERROR(transaction, "Verification failed: %s", strerror(errno));
        } else {
            TXLOG_ERROR(transaction, "Package signature or checksum is invalid");
        }
        
        strsplit_free(names);
        served_sm_post_event(&transaction->sm, SERVED_TX_EVENT_FAILED);
        return SM_ACTION_CONTINUE;
    }
    
    strsplit_free(names);
    served_sm_post_event(&transaction->sm, SERVED_TX_EVENT_OK);
    return SM_ACTION_CONTINUE;
}
