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

#include <runner.h>
#include <state.h>
#include <stdarg.h>
#include <stdio.h>
#include <transaction/sm.h>
#include <transaction/logging.h>
#include <utils.h>
#include <vlog.h>

// Protocol headers for event emission
#include "chef_served_service_server.h"

void served_transaction_log(
    struct served_transaction* transaction,
    enum served_transaction_log_level level,
    const char* format,
    ...)
{
    va_list args;
    char message[512];
    time_t timestamp;
    sm_state_t state;
    
    if (transaction == NULL || format == NULL) {
        return;
    }
    
    // Format the message
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    
    timestamp = time(NULL);
    state = served_sm_current_state(&transaction->sm);
    
    // Persist to state for non-ephemeral transactions
    if (transaction->type != SERVED_TRANSACTION_TYPE_EPHEMERAL) {
        served_state_lock();
        served_state_transaction_log_add(transaction->id, level, timestamp, state, message);
        served_state_unlock();
    }
    
    // Also log to vlog
    switch (level) {
    case SERVED_TRANSACTION_LOG_INFO:
        VLOG_DEBUG("served", "[Transaction %u] %s\n", transaction->id, message);
        break;
    case SERVED_TRANSACTION_LOG_WARNING:
        VLOG_WARNING("served", "[Transaction %u] %s\n", transaction->id, message);
        break;
    case SERVED_TRANSACTION_LOG_ERROR:
        VLOG_ERROR("served", "[Transaction %u] %s\n", transaction->id, message);
        break;
    }
    
    // Emit log event for non-ephemeral transactions
    if (transaction->type != SERVED_TRANSACTION_TYPE_EPHEMERAL) {
        enum chef_transaction_state     protocolState = served_transaction_map_state(state);
        enum chef_transaction_log_level protocolLevel;
        
        switch (level) {
        case SERVED_TRANSACTION_LOG_INFO:
            protocolLevel = CHEF_TRANSACTION_LOG_LEVEL_INFO;
            break;
        case SERVED_TRANSACTION_LOG_WARNING:
            protocolLevel = CHEF_TRANSACTION_LOG_LEVEL_WARNING;
            break;
        case SERVED_TRANSACTION_LOG_ERROR:
            protocolLevel = CHEF_TRANSACTION_LOG_LEVEL_ERROR;
            break;
        default:
            protocolLevel = CHEF_TRANSACTION_LOG_LEVEL_INFO;
            break;
        }
        
        chef_served_event_transaction_log_all(
            served_gracht_server(),
            &(struct chef_transaction_log) {
                .id = transaction->id,
                .entry = {
                    .level = protocolLevel,
                    .timestamp = (unsigned long long)timestamp,
                    .state = protocolState,
                    .message = message
                }
            }
        );
    }
}
