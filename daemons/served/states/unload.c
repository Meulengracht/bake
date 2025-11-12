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
#include <vlog.h>

enum sm_action_result served_handle_state_unload(void* context)
{
    struct served_transaction* transaction = context;
    int                        status;

    // Format name in the form publisher.package
    VLOG_DEBUG("served", "Unloading container for package %s\n",
        transaction->name
    );

    // Destroy the container running for publisher.package
    status = container_client_destroy_container(transaction->name);
    if (status) {
        VLOG_ERROR("served", "failed to destroy container for package %s\n",
            transaction->name
        );
        served_sm_post_event(&transaction->sm, SERVED_TX_EVENT_FAILED);
        return SM_ACTION_CONTINUE;
    }

    served_sm_post_event(&transaction->sm, SERVED_TX_EVENT_OK);
    return SM_ACTION_CONTINUE;
}
