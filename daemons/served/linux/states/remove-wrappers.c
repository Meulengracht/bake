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

static void __remove_application_symlinks(struct served_application* application)
{
    const char* mountRoot = served_application_get_mount_path(application);
    if (mountRoot == NULL) {
        // log
        return;
    }
    
    for (int i = 0; i < application->commands_count; i++) {
        int status = platform_unlink(application->commands[i].symlink);

        // then we free resources and NULL them so we are ready to remount
        free((char*)application->commands[i].symlink);
        free((char*)application->commands[i].data);
        application->commands[i].symlink = NULL;
        application->commands[i].data    = NULL;

        // and then we handle the error code, and by handling we mean just
        // log it, because we will ignore any issues encountered in this loop
        if (status != 0) {
            VLOG_WARNING("mount", "failed to remove symlink for command %s in app %s",
                application->commands[i].name, application->name);
        }
    }
    free((void*)mountRoot);
}

enum sm_action_result served_handle_state_remove_wrappers(void* context)
{
    struct served_transaction* transaction = context;

    served_sm_event(&transaction->sm, SERVED_TX_EVENT_OK);
    return SM_ACTION_CONTINUE;
}
