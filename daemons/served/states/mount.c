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

#include <transaction/states/mount.h>
#include <transaction/transaction.h>
#include <state.h>
#include <utils.h>

#include <chef/platform.h>

enum sm_action_result served_handle_state_mount(void* context)
{
    struct served_transaction* transaction = context;
    struct state_transaction*  state;
    struct state_application*  application;
    sm_event_t                 event = SERVED_TX_EVENT_FAILED;
    char**                     names = NULL;
    char*                      mountRoot = NULL;
    char*                      packPath = NULL;
    int                        status;

    served_state_lock();
    state = served_state_transaction(transaction->id);
    if (state == NULL) {
        served_state_unlock();
        served_sm_event(&transaction->sm, SERVED_TX_EVENT_FAILED);
        return SM_ACTION_CONTINUE;
    }
    application = served_state_application(state->name);
    if (application == NULL) {
        served_state_unlock();
        served_sm_event(&transaction->sm, SERVED_TX_EVENT_FAILED);
        return SM_ACTION_CONTINUE;
    }
    served_state_unlock();

    names = utils_split_package_name(state->name);
    if (names == NULL) {
        served_sm_event(&transaction->sm, SERVED_TX_EVENT_FAILED);
        return SM_ACTION_CONTINUE;
    }

    mountRoot = utils_path_mount(names[0], names[1]);
    packPath  = utils_path_pack(names[0], names[1]);
    if (mountRoot == NULL || packPath == NULL) {
        goto cleanup;
    }

    status = served_mount(packPath, mountRoot, &application->mount);
    if (status) {
        goto cleanup;
    }

    event = SERVED_TX_EVENT_OK;

cleanup:
    strsplit_free(names);
    free(mountRoot);
    free(packPath);
    served_sm_event(&transaction->sm, event);
    return SM_ACTION_CONTINUE;
}
