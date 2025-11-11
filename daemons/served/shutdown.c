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
    
    // Create a shutdown transaction to finalize any pending operations
    transactionId = served_state_transaction_new(&(struct served_transaction_options){
        .name        = "system-shutdown",
        .description = "Served system shutdown",
        .type        = SERVED_TRANSACTION_TYPE_EPHEMERAL,
        .stateSet    = &(struct served_sm_state_set){
            .states = g_stateSetShutdown,
            .states_count = 7
        },
        .initialState= 0
    });
    if (transactionId == (int)-1) {
        VLOG_ERROR("shutdown", "failed to create shutdown transaction\n");
        goto save_state;
    }
    VLOG_TRACE("shutdown", "created shutdown transaction %u\n", transactionId);
    served_runner_execute();

save_state:
    status = served_state_flush();
    if (status) {
        VLOG_ERROR("shutdown", "failed to save state!!!\n");
    }
}
