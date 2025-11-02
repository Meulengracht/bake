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

#include <transaction/states/load.h>
#include <transaction/transaction.h>
#include <state.h>

#include <chef/containerv.h>
#include <chef/platform.h>

static struct containerv_container* __create_container(const char* publisher, const char* package, const char* mountPath)
{
    struct containerv_container* container;
    struct containerv_options*   options;
    int                          status;
    char                         containerId[256];

    options = containerv_options_new();
    if (options == NULL) {
        return NULL;
    }

    // setup config
    containerv_options_set_caps(options, CV_CAP_FILESYSTEM | CV_CAP_PROCESS_CONTROL | CV_CAP_IPC);

    // format container id
    snprintf(&containerId[0], sizeof(containerId), "%s.%s", publisher, package);

    status = containerv_create(&containerId[0], mountPath, options, &container);
    containerv_options_delete(options);
    if (status) {
        return NULL;
    }
    return container;
}


enum sm_action_result served_handle_state_load(void* context)
{
    struct served_transaction* transaction = context;
    struct state_transaction*  state;
    struct state_application*  application;
    sm_event_t                 event = SERVED_TX_EVENT_FAILED;
    char**                     names = NULL;
    char*                      mountRoot = NULL;

    served_state_lock();
    state = served_state_transaction(transaction->id);
    if (state == NULL) {
        goto cleanup;
    }

    names = utils_split_package_name(state->name);
    if (names == NULL) {
        goto cleanup;
    }

    mountRoot = utils_path_mount(names[0], names[1]);
    if (mountRoot == NULL ) {
        goto cleanup;
    }

    application = served_state_application(state->name);
    if (application == NULL) {
        goto cleanup;
    }
    
    application->container = __create_container(mountRoot);
    if (application->container == NULL) {
        goto cleanup;
    }

    event = SERVED_TX_EVENT_OK;

cleanup:
    served_state_unlock();
    served_sm_event(&transaction->sm, event);
    return SM_ACTION_CONTINUE;
}
