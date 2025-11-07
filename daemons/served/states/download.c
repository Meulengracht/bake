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

#include <transaction/states/download.h>
#include <transaction/transaction.h>
#include <state.h>

#include <chef/store.h>
#include <chef/platform.h>
#include <vlog.h>

enum sm_action_result served_handle_state_download(void* context)
{
    struct served_transaction* transaction = context;
    struct state_transaction*  state;
    struct store_package       package = { NULL };
    int                        status;

    // hold the lock while reading
    served_state_lock();
    state = served_state_transaction(transaction->id);
    if (state == NULL) {
        served_state_unlock();
        served_sm_event(&transaction->sm, SERVED_TX_EVENT_FAILED);
        return SM_ACTION_CONTINUE;
    }

    // setup package parameters
    package.name = state->name;
    package.platform = CHEF_PLATFORM_STR; // always host
    package.arch = CHEF_ARCHITECTURE_STR; // always host
    package.channel = state->channel;
    package.revision = state->revision;

    // do not need the state anymore
    served_state_unlock();

    // TODO: update revision in state
    // TODO: progress reporting
    status = store_ensure_package(&package);
    if (status) {
        // todo retry detection here
        served_sm_event(&transaction->sm, SERVED_TX_EVENT_FAILED);
        return SM_ACTION_CONTINUE;
    }

    served_sm_event(&transaction->sm, SERVED_TX_EVENT_OK);
    return SM_ACTION_CONTINUE;
}

enum sm_action_result served_handle_state_download_retry(void* context)
{
    struct served_transaction* transaction = context;

    // TODO: wait for retry

    served_sm_event(&transaction->sm, SERVED_TX_EVENT_OK);
    return SM_ACTION_CONTINUE;
}
