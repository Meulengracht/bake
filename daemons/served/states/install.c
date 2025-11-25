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

#include <chef/package.h>
#include <chef/platform.h>
#include <chef/store.h>
#include <vlog.h>
#include <stdlib.h>

static struct state_application* __application_new(const char* name)
{
    struct state_application* application;

    application = calloc(1, sizeof(struct state_application));
    if (application == NULL) {
        return NULL;
    }

    application->name = name;
    return application;
}

static int __application_add_revision(struct state_application* application, const char* channel, struct chef_version* version)
{
    application->revisions = realloc(application->revisions, application->revisions_count + 1);
    if (application->revisions == NULL) {
        return -1;
    }

    application->revisions[application->revisions_count].tracking_channel = channel;
    application->revisions[application->revisions_count].version = version;
    application->revisions_count++;
    return 0;
}

static int __load_application_package(struct state_transaction* state, const char* path, struct state_application** applicationOut)
{
    struct state_application* application;
    struct chef_package*      package;
    struct chef_version*      version;
    struct chef_command*      commands;
    int                       count;
    int                       status;

    status = chef_package_load(
        path, &package, &version, &commands, &count
    );
    if (status) {
        return status;
    }

    application = __application_new(state->name);
    if (application == NULL) {
        chef_version_free(version);
        status = -1;
        goto cleanup;
    }
    
    version->revision = state->revision;
    status = __application_add_revision(application, state->channel, version);
    if (status) {
        chef_version_free(version);
        goto cleanup;
    }

    application->commands_count = count;
    if (count) {
        application->commands = calloc(count, sizeof(struct state_application_command));
        if (application->commands == NULL) {
            chef_version_free(version);
            status = -1;
            goto cleanup;
        }

        for (int i = 0; i < count; i++) {
            application->commands[i].type      = commands[i].type;
            application->commands[i].name      = platform_strdup(commands[i].name);
            application->commands[i].path      = platform_strdup(commands[i].path);
            application->commands[i].arguments = commands[i].arguments ? platform_strdup(commands[i].arguments) : NULL;
        }
    }

    *applicationOut = application;

cleanup:
    chef_package_free(package);
    chef_commands_free(commands, count);
    return status;
}

enum sm_action_result served_handle_state_install(void* context)
{
    struct served_transaction* transaction = context;
    struct state_transaction*  state;
    struct state_application*  application;
    sm_event_t                 event = SERVED_TX_EVENT_FAILED;
    char*                      storagePath = NULL;
    const char*                path;
    char**                     names = NULL;
    int                        status;

    served_state_lock();
    state = served_state_transaction(transaction->id);
    if (state == NULL) {
        served_state_unlock();
        served_sm_post_event(&transaction->sm, SERVED_TX_EVENT_FAILED);
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
    status = __load_application_package(state, path, &application);
    if (status) {
        served_state_unlock();
        goto cleanup;
    }

    status = served_state_add_application(application);
    if (status) {
        served_state_unlock();
        goto cleanup;
    }
    served_state_unlock();
    
    event = SERVED_TX_EVENT_OK;

cleanup:
    strsplit_free(names);
    free((void*)storagePath);
    served_sm_post_event(&transaction->sm, event);
    return SM_ACTION_CONTINUE;
}
