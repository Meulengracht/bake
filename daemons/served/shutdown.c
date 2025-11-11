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

#include <startup.h>
#include <state.h>
#include <runner.h>
#include <vlog.h>

#include <transaction/sets.h>
#include <transaction/transaction.h>

void served_shutdown(void)
{
    unsigned int transactionId;
    int          status;
    VLOG_TRACE("shutdown", "served_shutdown()\n");

    if (!served_runner_is_running()) {
        VLOG_DEBUG("shutdown", "runner thread not running, skipping shutdown operations\n");
        goto cleanup_state;
    }
    
    (void)served_transaction_create(&(struct served_transaction_options){
        .name        = "system-shutdown",
        .description = "Served system shutdown",
        .type        = SERVED_TRANSACTION_TYPE_EPHEMERAL,
        .stateSet    = &(struct served_sm_state_set){
            .states = g_stateSetShutdown,
            .states_count = 7
        },
    });

    VLOG_DEBUG("shutdown", "requesting runner thread to stop\n");
    status = served_runner_stop();
    if (status) {
        VLOG_ERROR("shutdown", "failed to stop runner thread cleanly\n");
    }

cleanup_state:
    // Flush state to ensure all changes are persisted
    VLOG_DEBUG("shutdown", "flushing state to disk\n");
    status = served_state_flush();
    if (status) {
        VLOG_ERROR("shutdown", "failed to save state!!!\n");
    } else {
        VLOG_DEBUG("shutdown", "state flushed successfully\n");
    }
    
    // Close state database
    VLOG_TRACE("shutdown", "shutdown complete\n");
}
