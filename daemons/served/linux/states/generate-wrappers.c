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

#include <transaction/states/generate-wrappers.h>
#include <transaction/transaction.h>
#include <state.h>

static int __create_application_symlinks(struct served_application* application)
{
    const char* mountRoot = served_application_get_mount_path(application);
    if (mountRoot == NULL) {
        return -1;
    }

    for (int i = 0; i < application->commands_count; i++) {
        struct served_command* command = &application->commands[i];
        const char*            symlinkPath;
        const char*            dataPath;
        int                    status;

        symlinkPath = served_application_get_command_symlink_path(application, command);
        dataPath    = served_application_get_data_path(application);
        if (symlinkPath == NULL || dataPath == NULL) {
            free((void*)symlinkPath);
            free((void*)dataPath);
            VLOG_WARNING("mount", "failed to allocate paths for command %s in app %s",
                command->name, application->name);
            continue;
        }

        // create a link from /chef/bin/<command> => ${CHEF_INSTALL_DIR}/libexec/chef/serve-exec
        status = platform_symlink(symlinkPath, CHEF_INSTALL_DIR "/libexec/chef/serve-exec", 0);
        if (status != 0) {
            free((void*)symlinkPath);
            free((void*)dataPath);
            VLOG_WARNING("mount", "failed to create symlink for command %s in app %s",
                command->name, application->name);
            continue;
        }

        // store the command mount path which is read by serve-exec
        command->symlink = symlinkPath;
        command->data    = dataPath;
    }
    free((void*)mountRoot);
    return 0;
}

enum sm_action_result served_handle_state_generate_wrappers(void* context)
{
    struct served_transaction* transaction = context;

    served_sm_event(&transaction->sm, SERVED_TX_EVENT_OK);
    return SM_ACTION_CONTINUE;
}
