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

#include <chef/package_manifest.h>
#include <chef/platform.h>
#include <chef/store.h>
#include <vlog.h>
#include <stdlib.h>

static struct chef_version* __duplicate_version(const struct chef_version* source)
{
    struct chef_version* version;

    version = calloc(1, sizeof(struct chef_version));
    if (version == NULL) {
        return NULL;
    }

    version->major = source->major;
    version->minor = source->minor;
    version->patch = source->patch;
    version->revision = source->revision;
    version->size = source->size;
    version->created = source->created ? platform_strdup(source->created) : NULL;
    version->tag = source->tag ? platform_strdup(source->tag) : NULL;
    if ((source->created != NULL && version->created == NULL)
     || (source->tag != NULL && version->tag == NULL)) {
        chef_version_free(version);
        return NULL;
    }
    return version;
}

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
    struct state_application_revision* revisions;

    revisions = realloc(
        application->revisions,
        (application->revisions_count + 1) * sizeof(struct state_application_revision)
    );
    if (revisions == NULL) {
        return -1;
    }

    application->revisions = revisions;

    application->revisions[application->revisions_count].tracking_channel = channel;
    application->revisions[application->revisions_count].version = version;
    application->revisions_count++;
    return 0;
}

static int __load_application_package(struct state_transaction* state, const char* path, struct state_application** applicationOut)
{
    struct state_application*    application;
    struct chef_package_manifest* manifest = NULL;
    struct chef_version*          version = NULL;
    int                           status;

    status = chef_package_manifest_load(path, &manifest);
    if (status) {
        return status;
    }

    application = __application_new(state->name);
    if (application == NULL) {
        status = -1;
        goto cleanup;
    }

    application->base = manifest->base ? platform_strdup(manifest->base) : NULL;
    if (manifest->base != NULL && application->base == NULL) {
        status = -1;
        goto cleanup;
    }
    version = __duplicate_version(&manifest->version);
    if (version == NULL) {
        status = -1;
        goto cleanup;
    }

    version->revision = state->revision;
    status = __application_add_revision(application, state->channel, version);
    if (status) {
        chef_version_free(version);
        goto cleanup;
    }

    application->commands_count = (int)manifest->commands_count;
    if (manifest->commands_count > 0) {
        application->commands = calloc(manifest->commands_count, sizeof(struct state_application_command));
        if (application->commands == NULL) {
            status = -1;
            goto cleanup;
        }

        for (size_t i = 0; i < manifest->commands_count; i++) {
            application->commands[i].type = manifest->commands[i].type;
            application->commands[i].name = platform_strdup(manifest->commands[i].name);
            application->commands[i].path = platform_strdup(manifest->commands[i].path);
            application->commands[i].arguments = manifest->commands[i].arguments
                ? platform_strdup(manifest->commands[i].arguments)
                : NULL;
            if (application->commands[i].name == NULL || application->commands[i].path == NULL
             || (manifest->commands[i].arguments != NULL && application->commands[i].arguments == NULL)) {
                status = -1;
                goto cleanup;
            }
        }
    }

    *applicationOut = application;

cleanup:
    chef_package_manifest_free(manifest);
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
    int                        revision;

    served_state_lock();
    state = served_state_transaction(transaction->id);
    if (state == NULL) {
        served_state_unlock();
        served_sm_post_event(&transaction->sm, SERVED_TX_EVENT_FAILED);
        return SM_ACTION_CONTINUE;
    }

    names = utils_split_package_name(state->name);
    revision = state->revision;
    served_state_unlock();

    if (names == NULL) {
        goto cleanup;
    }

    if (revision < 0) {
        path = utils_path_local_pack(names[0], names[1], revision);
        if (path == NULL) {
            goto cleanup;
        }
    } else {
        status = store_package_path(&(struct store_package) {
            .name = state->name,
            .platform = CHEF_PLATFORM_STR,
            .arch = CHEF_ARCHITECTURE_STR,
            .channel = NULL,
            .revision = revision
        }, &path);
        if (status) {
            VLOG_ERROR("served", "could not find the revision %i for %s\n", revision, state->name);
            goto cleanup;
        }
    }

    storagePath = utils_path_pack(names[0], names[1], revision);
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
    if (state != NULL && state->revision < 0) {
        free((void*)path);
    }
    free((void*)storagePath);
    served_sm_post_event(&transaction->sm, event);
    return SM_ACTION_CONTINUE;
}
