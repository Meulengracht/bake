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

void chef_served_remove_invocation(struct gracht_message* message, const char* packageName)
{
    unsigned int transactionId;
    char         nameBuffer[256];
    char         descriptionBuffer[512];
    VLOG_DEBUG("api", "chef_served_remove_invocation(package=%s)\n", packageName);

    snprintf(nameBuffer, sizeof(nameBuffer), "Remove via API (%s)", packageName);
    snprintf(descriptionBuffer, sizeof(descriptionBuffer), "Removal of package '%s' requested via served API", packageName);

    served_state_lock();
    transactionId = served_state_transaction_new(&(struct served_transaction_options){
        .name = &nameBuffer[0],
        .description = &descriptionBuffer[0],
        .type = SERVED_TRANSACTION_TYPE_UNINSTALL,
    });

    served_state_transaction_state_new(
        transactionId,
        &(struct state_transaction){
            .name = packageName,
        }
    );
    served_state_unlock();
    chef_served_remove_response(message, transactionId);
}
