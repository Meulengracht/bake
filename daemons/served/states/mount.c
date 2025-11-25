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
#include <stdlib.h>
#include <vlog.h>

static int __mount_application(const char* name)
{
    struct state_application* application;
    char**                    names = NULL;
    char*                     mountRoot = NULL;
    char*                     packPath = NULL;
    int                       status = -1;

    application = served_state_application(name);
    if (application == NULL) {
        return -1;
    }

    names = utils_split_package_name(name);
    if (names == NULL) {
        return -1;
    }

    mountRoot = utils_path_mount(names[0], names[1]);
    packPath  = utils_path_pack(names[0], names[1]);
    if (mountRoot == NULL || packPath == NULL) {
        goto cleanup;
    }

    status = served_mount(packPath, mountRoot, &application->mount);

cleanup:
    strsplit_free(names);
    free(mountRoot);
    free(packPath);
    return status;
}

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
        served_sm_post_event(&transaction->sm, SERVED_TX_EVENT_FAILED);
        return SM_ACTION_CONTINUE;
    }
    application = served_state_application(state->name);
    if (application == NULL) {
        served_state_unlock();
        served_sm_post_event(&transaction->sm, SERVED_TX_EVENT_FAILED);
        return SM_ACTION_CONTINUE;
    }
    served_state_unlock();

    if (__mount_application(state->name)) {
        VLOG_ERROR("served", "Failed to mount application %s\n", state->name);
        goto cleanup;
    }
    
    event = SERVED_TX_EVENT_OK;

cleanup:
    strsplit_free(names);
    free(mountRoot);
    free(packPath);
    served_sm_post_event(&transaction->sm, event);
    return SM_ACTION_CONTINUE;
}

enum sm_action_result served_handle_state_mount_all(void* context)
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
        if (__mount_application(app->name)) {
            goto cleanup;
        }
    }

    event = SERVED_TX_EVENT_OK;

cleanup:
    served_state_unlock();
    served_sm_post_event(&transaction->sm, event);
    return SM_ACTION_CONTINUE;
}
