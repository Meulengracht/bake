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

#include <gracht/server.h>
#include <state.h>
#include <stdio.h>
#include <vlog.h>

#include <transaction/transaction.h>

#include "chef_served_service_server.h"

void chef_served_update_invocation(struct gracht_message* message, const struct chef_served_update_options* options)
{
    unsigned int transactionId;
    char         nameBuffer[256];
    char         descriptionBuffer[512];
    
    // Update options now contains an array of packages to update
    // For now, we'll handle the first package if any are provided
    if (options->packages_count == 0) {
        VLOG_WARNING("api", "chef_served_update_invocation: no packages specified\n");
        return;
    }
    
    VLOG_DEBUG("api", "chef_served_update_invocation(package=%s)\n", options->packages[0].name);

    snprintf(nameBuffer, sizeof(nameBuffer), "Update via API (%s)", options->packages[0].name);
    snprintf(descriptionBuffer, sizeof(descriptionBuffer), "Update of package '%s' requested via served API", options->packages[0].name);

    served_state_lock();
    transactionId = served_state_transaction_new(&(struct served_transaction_options){
        .name = &nameBuffer[0],
        .description = &descriptionBuffer[0],
        .type = SERVED_TRANSACTION_TYPE_UPDATE,
    });

    served_state_transaction_state_new(
        transactionId,
        &(struct state_transaction){
            .name = options->packages[0].name,
            .channel = NULL,  // Update options don't specify channel
            .revision = 0,    // Update options don't specify revision
        }
    );
    served_state_unlock();
    chef_served_update_response(message, transactionId);
}
