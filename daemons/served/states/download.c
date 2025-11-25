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
#include <gracht/server.h>
#include <transaction/states/download.h>
#include <transaction/states/types.h>
#include <transaction/transaction.h>
#include <transaction/logging.h>
#include <state.h>
#include <utils.h>

#include <chef/store.h>
#include <chef/platform.h>
#include <vlog.h>

// Protocol headers for event emission
#include "chef_served_service_server.h"

// Progress reporting threshold (only report every 5% change)
#define PROGRESS_REPORT_THRESHOLD 5

static void __emit_io_progress(
    unsigned long long bytes_current,
    unsigned long long bytes_total,
    void*              context)
{
    struct served_transaction* transaction = context;
    unsigned int               percentage;
    
    if (bytes_total == 0) {
        return;
    }
    
    percentage = (unsigned int)((bytes_current * 100) / bytes_total);
    
    // Only emit if percentage changed significantly
    if (percentage < transaction->io_progress.last_reported_percentage + PROGRESS_REPORT_THRESHOLD &&
        bytes_current < bytes_total) {
        return;
    }
    
    transaction->io_progress.bytes_current = bytes_current;
    transaction->io_progress.bytes_total = bytes_total;
    transaction->io_progress.last_reported_percentage = percentage;
    
    chef_served_event_transaction_io_progress_all(
        served_gracht_server(),
        &(struct chef_transaction_io_progress) {
            .id = transaction->id,
            .state = CHEF_TRANSACTION_STATE_DOWNLOADING,
            .bytes_current = bytes_current,
            .bytes_total = bytes_total,
            .percentage = percentage
        }
    );
    
    VLOG_DEBUG("served", "Download progress: %llu/%llu (%u%%)\n",
               bytes_current, bytes_total, percentage);
}

enum sm_action_result served_handle_state_download(void* context)
{
    struct served_transaction* transaction = context;
    struct state_transaction*  state;
    struct store_package       package = { NULL };
    int                        status;

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

    package.name = state->name;
    package.platform = CHEF_PLATFORM_STR; // always host
    package.arch = CHEF_ARCHITECTURE_STR; // always host
    package.channel = state->channel;
    package.revision = state->revision;
    
    served_state_unlock();

    status = store_ensure_package(
        &package,
        &(struct chef_observer){
            .report = __emit_io_progress,
            .userData = transaction
        }
    );

    if (status) {
        if (errno == ENOSPC) {
            TXLOG_ERROR(transaction, "Insufficient disk space to download package");
        } else if (errno == EACCES || errno == EPERM) {
            TXLOG_ERROR(transaction, "Permission denied while downloading package");
        } else if (errno == ETIMEDOUT || errno == ENETUNREACH || errno == EHOSTUNREACH) {
            TXLOG_ERROR(transaction, "Network error while downloading package (check connectivity)");
        } else if (errno != 0) {
            TXLOG_ERROR(transaction, "Download failed: %s", strerror(errno));
        } else {
            TXLOG_ERROR(transaction, "Failed to download package (unknown error)");
        }
        
        served_sm_post_event(&transaction->sm, SERVED_TX_EVENT_FAILED);
        return SM_ACTION_CONTINUE;
    }

    TXLOG_INFO(transaction, "Package downloaded successfully");
    served_sm_post_event(&transaction->sm, SERVED_TX_EVENT_OK);
    return SM_ACTION_CONTINUE;
}

enum sm_action_result served_handle_state_download_retry(void* context)
{
    struct served_transaction* transaction = context;

    // TODO: wait for retry

    served_sm_post_event(&transaction->sm, SERVED_TX_EVENT_OK);
    return SM_ACTION_CONTINUE;
}
