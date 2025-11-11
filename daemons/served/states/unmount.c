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

#include <transaction/states/unmount.h>
#include <transaction/transaction.h>
#include <state.h>
#include <utils.h>

enum sm_action_result served_handle_state_unmount(void* context)
{
    struct served_transaction* transaction = context;
    struct state_transaction*  state;
    struct state_application*  application;

    served_state_lock();
    state = served_state_transaction(transaction->id);
    if (state == NULL) {
        goto cleanup;
    }

    application = served_state_application(state->name);
    if (application == NULL) {
        goto cleanup;
    }

    if (application->mount != NULL) {
        served_unmount(application->mount);
    }

cleanup:
    served_state_unlock();
    served_sm_post_event(&transaction->sm, SERVED_TX_EVENT_OK);
    return SM_ACTION_CONTINUE;
}
