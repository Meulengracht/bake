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
#include <transaction/states/verify.h>
#include <transaction/transaction.h>
#include <state.h>
#include <utils.h>

static char** __split_name(const char* name)
{
    // split the publisher/package
    int    namesCount = 0;
    char** names = strsplit(name, '/');
    if (names == NULL) {
        VLOG_ERROR("store", "__find_package_in_inventory: invalid package naming '%s' (must be publisher/package)\n", name);
        return NULL;
    }

    while (names[namesCount] != NULL) {
        namesCount++;
    }

    if (namesCount != 2) {
        VLOG_ERROR("store", "__find_package_in_inventory: invalid package naming '%s' (must be publisher/package)\n", name);
        strsplit_free(names);
        return NULL;
    }
    return names;
}

enum sm_action_result served_handle_state_verify(void* context)
{
    struct served_transaction* transaction = context;
    struct state_transaction*  state;
    int                        status;
    char**                     names;

    const char* name;
    int revision;

    served_state_lock();
    name = state->name;
    revision = state->revision;
    served_state_unlock();
    
    state = served_state_transaction(transaction->id);
    if (state == NULL) {
        served_sm_event(&transaction->sm, SERVED_TX_EVENT_FAILED);
        return SM_ACTION_CONTINUE;
    }

    names = __split_name(state->name);
    if (names == NULL) {
        served_sm_event(&transaction->sm, SERVED_TX_EVENT_FAILED);
        return SM_ACTION_CONTINUE;
    }

    status = utils_verify_package(
        names[0], names[1], state->revision
    );
    strsplit_free(names);
    if (status) {
        served_sm_event(&transaction->sm, SERVED_TX_EVENT_FAILED);
        return SM_ACTION_CONTINUE;
    }

    served_sm_event(&transaction->sm, SERVED_TX_EVENT_OK);
    return SM_ACTION_CONTINUE;
}
