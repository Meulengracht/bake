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

#include <transaction/states/remove-wrappers.h>
#include <transaction/transaction.h>
#include <state.h>
#include <utils.h>

#include <chef/platform.h>
#include <stdlib.h>
#include <vlog.h>

static int __remove_wrappers(const char* name)
{
    struct state_application*  application;

    application = served_state_application(name);
    if (application == NULL) {
        return -1;
    }

    for (int i = 0; i < application->commands_count; i++) {
        int   status;
        char* wrapperPath;

        if (application->commands[i].type != CHEF_COMMAND_TYPE_EXECUTABLE) {
            continue;
        }

        wrapperPath = utils_path_command_wrapper(application->commands[i].name);
        if (wrapperPath == NULL) {
            VLOG_ERROR("remove-wrappers", "%s.%s: cannot allocate memory for wrapper-path\n", name, application->commands[i].name);
            continue;
        }

        status = platform_unlink(wrapperPath);
        if (status) {
            VLOG_ERROR("remove-wrappers", "%s.%s: failed to remove %s\n", name, application->commands[i].name, wrapperPath);
            // fall-through
        }
        free(wrapperPath);
    }
    return 0;
}

enum sm_action_result served_handle_state_remove_wrappers(void* context)
{
    struct served_transaction* transaction = context;
    struct state_transaction*  state;

    served_state_lock();
    state = served_state_transaction(transaction->id);
    if (state == NULL) {
        goto cleanup;
    }

    __remove_wrappers(state->name);

cleanup:
    served_state_unlock();
    served_sm_post_event(&transaction->sm, SERVED_TX_EVENT_OK);
    return SM_ACTION_CONTINUE;
}

enum sm_action_result served_handle_state_remove_wrappers_all(void* context)
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
        if (__remove_wrappers(app->name)) {
            goto cleanup;
        }
    }

    event = SERVED_TX_EVENT_OK;

cleanup:
    served_state_unlock();
    served_sm_post_event(&transaction->sm, event);
    return SM_ACTION_CONTINUE;
}
