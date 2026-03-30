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
#include <gracht/server.h>
#include <runner.h>
#include <state.h>
#include <stdio.h>
#include <stdlib.h>
#include <vlog.h>

#include <transaction/transaction.h>
#include <transaction/logging.h>

#include "chef_served_service_server.h"

void chef_served_logs_invocation(struct gracht_message* message, unsigned int transaction_id)
{
    struct state_transaction_log*      logs = NULL;
    struct chef_transaction_log_entry* entries = NULL;
    int                                count = 0;
    int                                i;
    
    VLOG_DEBUG("api", "chef_served_logs_invocation(transaction_id=%u)\n", transaction_id);
    
    served_state_lock();
    if (served_state_transaction_logs(transaction_id, &logs, &count) != 0 || count == 0) {
        served_state_unlock();
        chef_served_logs_response(message, NULL, 0);
        return;
    }
    
    // Allocate array for response
    entries = calloc(count, sizeof(struct chef_transaction_log_entry));
    if (entries == NULL) {
        served_state_unlock();
        VLOG_ERROR("api", "failed to allocate memory for log entries\n");
        chef_served_logs_response(message, NULL, 0);
        return;
    }
    
    // Copy log entries from state
    for (i = 0; i < count; i++) {
        struct state_transaction_log* log = &logs[i];
        enum chef_transaction_log_level level;
        enum chef_transaction_state state;
        
        // Map log level
        switch (log->level) {
        case SERVED_TRANSACTION_LOG_INFO:
            level = CHEF_TRANSACTION_LOG_LEVEL_INFO;
            break;
        case SERVED_TRANSACTION_LOG_WARNING:
            level = CHEF_TRANSACTION_LOG_LEVEL_WARNING;
            break;
        case SERVED_TRANSACTION_LOG_ERROR:
            level = CHEF_TRANSACTION_LOG_LEVEL_ERROR;
            break;
        default:
            level = CHEF_TRANSACTION_LOG_LEVEL_INFO;
            break;
        }
        
        // Map state using the public mapping function
        state = served_transaction_map_state(log->state);
        
        entries[i].level = level;
        entries[i].timestamp = (unsigned long long)log->timestamp;
        entries[i].state = state;
        entries[i].message = platform_strdup(log->message);
    }
    
    served_state_unlock();
    
    chef_served_logs_response(message, entries, count);
    
    // Cleanup
    for (i = 0; i < count; i++) {
        free(entries[i].message);
    }
    free(entries);
}
