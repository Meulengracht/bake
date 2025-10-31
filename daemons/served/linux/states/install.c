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

#include <transaction/states/install.h>
#include <transaction/transaction.h>
#include <state.h>
#include <utils.h>

#include <chef/platform.h>
#include <chef/store.h>
#include <vlog.h>

static int __parse_package(const char* publisher, const char* path, struct state_application** applicationOut)
{
    struct served_application* application;
    struct chef_package*       package;
    struct chef_version*       version;
    struct chef_command*       commands;
    int                        count;
    int                        status;

    status = chef_package_load(path,
                               &package,
                               &version,
                               &commands,
                               &count
    );
    if (status) {
        return status;
    }

    // In theory this should also verify the signed signature...
    application = served_application_new();
    if (application == NULL) {
        status = -1;
        goto cleanup;
    }

    application->name = __build_application_name(publisher, package->package);
    if (application->name == NULL) {
        status = -1;
        goto cleanup;
    }

    application->publisher = strdup(publisher);
    application->package   = strdup(package->package);
    if (application->publisher == NULL || application->package == NULL) {
        status = -1;
        goto cleanup;
    }

    application->major    = version->major;
    application->minor    = version->minor;
    application->patch    = version->patch;
    application->revision = version->revision;

    application->commands_count = count;
    if (count) {
        application->commands = calloc(count, sizeof(struct served_command));
        if (application->commands == NULL) {
            status = -1;
            goto cleanup;
        }

        for (int i = 0; i < count; i++) {
            application->commands[i].type      = (int)commands[i].type;
            application->commands[i].name      = strdup(commands[i].name);
            application->commands[i].path      = strdup(commands[i].path);
            application->commands[i].arguments = commands[i].arguments ? strdup(commands[i].arguments) : NULL;
        }
    }

    *applicationOut = application;

cleanup:
    chef_package_free(package);
    chef_version_free(version);
    chef_commands_free(commands, count);
    if (status) {
        served_application_delete(application);
    }
    return status;
}

enum sm_action_result served_handle_state_install(void* context)
{
    struct served_transaction* transaction = context;
    struct state_transaction*  state;
    sm_event_t                 event = SERVED_TX_EVENT_FAILED;
    char*                      storagePath = NULL;
    const char*                path;
    char**                     names = NULL;
    int                        status;

    served_state_lock();
    state = served_state_transaction(transaction->id);
    if (state == NULL) {
        served_state_unlock();
        served_sm_event(&transaction->sm, SERVED_TX_EVENT_FAILED);
        return SM_ACTION_CONTINUE;
    }
    served_state_unlock();

    names = utils_split_package_name(state->name);
    if (names == NULL) {
        goto cleanup;
    }

    status = store_package_path(&(struct store_package) {
        .name = state->name,
        .platform = CHEF_PLATFORM_STR,
        .arch = CHEF_ARCHITECTURE_STR,
        .channel = NULL,
        .revision = state->revision
    }, &path);
    if (status) {
        VLOG_ERROR("served", "could not find the revision %i for %s\n", state->revision, state->name);
        goto cleanup;
    }

    storagePath = utils_path_pack(names[0], names[1]);
    if (storagePath == NULL) {
        goto cleanup;
    }

    status = platform_copyfile(path, storagePath);
    if (status) {
        goto cleanup;
    }

    served_state_lock();
    status = served_state_application_new(state->name, state->channel, state->revision);
    served_state_unlock();
    if (status) {
        goto cleanup;
    }
    
    event = SERVED_TX_EVENT_OK;

cleanup:
    strsplit_free(names);
    free((void*)storagePath);
    served_sm_event(&transaction->sm, event);
    return SM_ACTION_CONTINUE;
}
