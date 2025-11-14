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
#include <utils.h>

#include <chef/platform.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>

static int __load_application(const char* name)
{
    struct state_application* application;
    char**                    names = NULL;
    char*                     mountRoot = NULL;
    char                      containerId[256];
    int                       status;
    
    names = utils_split_package_name(name);
    if (names == NULL) {
        return -1;
    }

    mountRoot = utils_path_mount(names[0], names[1]);
    if (mountRoot == NULL) {
        strsplit_free(names);
        return -1;
    }
    snprintf(&containerId[0], sizeof(containerId), "%s.%s", names[0], names[1]);
    strsplit_free(names);

    application = served_state_application(name);
    if (application == NULL) {
        free(mountRoot);
        return -1;
    }

    status = container_client_create_container(&(struct container_options){
        .id = &containerId[0],
        .rootfs = mountRoot,
    });
    free(mountRoot);
    if (status && errno != EEXIST) {
        return -1;
    }

    application->container_id = platform_strdup(&containerId[0]);
    return 0;
}

enum sm_action_result served_handle_state_load(void* context)
{
    struct served_transaction* transaction = context;
    struct state_transaction*  state;
    sm_event_t                 event = SERVED_TX_EVENT_FAILED;

    served_state_lock();
    state = served_state_transaction(transaction->id);
    if (state == NULL) {
        goto cleanup;
    }

    if (__load_application(state->name)) {
        goto cleanup;
    }

    event = SERVED_TX_EVENT_OK;

cleanup:
    served_state_unlock();
    served_sm_post_event(&transaction->sm, event);
    return SM_ACTION_CONTINUE;
}

enum sm_action_result served_handle_state_load_all(void* context)
{
    struct served_transaction* transaction = context;
    struct state_application*  applications;
    int                        count;
    int                        status;
    sm_event_t                 event = SERVED_TX_EVENT_FAILED;
    
    served_state_lock();
    status = served_state_get_applications(&applications, &count);
    if (status) {
        goto cleanup;
    }

    for (int i = 0; i < count; i++) {
        struct state_application* app = &applications[i];
        if (__load_application(app->name)) {
            goto cleanup;
        }
    }

    event = SERVED_TX_EVENT_OK;

cleanup:
    served_state_unlock();
    served_sm_post_event(&transaction->sm, event);
    return SM_ACTION_CONTINUE;
}
