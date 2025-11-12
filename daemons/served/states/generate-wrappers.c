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
#include <utils.h>

#include <chef/platform.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vlog.h>

static const char* g_wrapperTemplate = 
"#!/bin/sh\n"
"%s --container %s --path %s %s\n";

static char* __serve_exec_path(void)
{
    char   buffer[PATH_MAX] = { 0 };
    char   dirnm[PATH_MAX] = { 0 };
    size_t index;
    char*  p;
    int    status;
    VLOG_DEBUG("bake", "__serve_exec_path()\n");

    status = readlink("/proc/self/exe", &buffer[0], PATH_MAX);
    if (status < 0) {
        VLOG_ERROR("bake", "__install_bakectl: failed to read /proc/self/exe\n");
        return NULL;
    }

    p = strrchr(&buffer[0], CHEF_PATH_SEPARATOR);
    if (p == NULL) {
        VLOG_ERROR("bake", "__install_bakectl: could not find separator in %s\n", &buffer[0]);
        return NULL;
    }

    index = (p + 1) - (&buffer[0]);
    strncpy(&dirnm[0], &buffer[0], index);
    snprintf(&buffer[0], sizeof(buffer), "%sserve-exec", &dirnm[0]);
    return platform_strdup(&buffer[0]);
}

static int __write_wrapper(const char* wrapperPath, const char* sexecPath, const char* container, const char* path, const char* arguments)
{
    FILE* wrapper;

    wrapper = fopen(wrapperPath, "w");
    if (wrapper == NULL) {
        return -1;
    }

    fprintf(wrapper, g_wrapperTemplate, sexecPath, container, path, arguments);
    fclose(wrapper);
    return 0;
}

static void __format_container_name(const char* name, char* buffer)
{
    int i;

    // copy the entire string first, then we replace '/' with '.'
    strcpy(buffer, name);
    for (i = 0; buffer[i]; i++) {
        if (buffer[i] == '/') {
            buffer[i] = '.';
            break;
        }
    }
}

static int __generate_wrappers(const char* appName)
{
    struct state_application* application;
    char*                     sexecPath = __serve_exec_path();
    char                      name[CHEF_PACKAGE_ID_LENGTH_MAX];

    application = served_state_application(appName);
    if (application == NULL) {
        goto cleanup;
    }

    // construct the container id
    __format_container_name(appName, &name[0]);

    for (int i = 0; i < application->commands_count; i++) {
        int   status;
        char* wrapperPath;

        if (application->commands[i].type != CHEF_COMMAND_TYPE_EXECUTABLE) {
            continue;
        }

        wrapperPath = utils_path_command_wrapper(application->commands[i].name);
        if (wrapperPath == NULL) {
            VLOG_ERROR("generate-wrappers", "%s.%s: cannot allocate memory for wrapper-path\n", appName, application->commands[i].name);
            continue;
        }

        status = __write_wrapper(
            wrapperPath,
            sexecPath,
            &name[0],
            application->commands[i].path,
            application->commands[i].arguments
        );
        if (status) {
            VLOG_ERROR("generate-wrappers", "%s.%s: failed to write wrapper to %s\n", wrapperPath);
            // fall-through
        }
        free(wrapperPath);
    }

cleanup:
    free(sexecPath);
    return 0;
}

enum sm_action_result served_handle_state_generate_wrappers(void* context)
{
    struct served_transaction* transaction = context;
    struct state_transaction*  state;

    served_state_lock();
    state = served_state_transaction(transaction->id);
    if (state == NULL) {
        goto cleanup;
    }

    if (__generate_wrappers(state->name)) {
        goto cleanup;
    }

cleanup:
    served_state_unlock();
    served_sm_post_event(&transaction->sm, SERVED_TX_EVENT_OK);
    return SM_ACTION_CONTINUE;
}

enum sm_action_result served_handle_state_generate_wrappers_all(void* context)
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
        if (__generate_wrappers(app->name)) {
            goto cleanup;
        }
    }

    event = SERVED_TX_EVENT_OK;

cleanup:
    served_state_unlock();
    served_sm_post_event(&transaction->sm, event);
    return SM_ACTION_CONTINUE;
}
