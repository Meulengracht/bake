/**
 * Copyright 2022, Philip Meulengracht
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

#include <errno.h>
#include <application.h>
#include <linux/limits.h>
#include <chef/platform.h>
#include <jansson.h>
#include <state.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <vlog.h>

struct __state {
    struct served_application** applications;
    int                         application_count;
};

static struct __state* g_state = NULL;

static struct __state* __state_new(void)
{
    struct __state* state;

    state = malloc(sizeof(struct __state));
    if (state == NULL) {
        return NULL;
    }

    memset(state, 0, sizeof(struct __state));
    return state;
}

static void __state_destroy(struct __state* state)
{
    if (state == NULL) {
        return;
    }

    for (int i = 0; i < state->application_count; i++) {
        served_application_delete(state->applications[i]);
    }
    free((void*)state->applications);
    free((void*)state);
}

static const char* __get_string_safe(json_t* json, const char* key)
{
    json_t* member;

    member = json_object_get(json, key);
    if (member) {
        return strdup(json_string_value(member));
    }
    return NULL;
}

static int __parse_command(json_t* cmd, struct served_command* command)
{
    command->name      = __get_string_safe(cmd, "name");
    command->path      = __get_string_safe(cmd, "path");
    command->arguments = __get_string_safe(cmd, "args");
    command->type      = (int)json_integer_value(json_object_get(cmd, "type"));
    if (command->name == NULL || command->path == NULL) {
        VLOG_ERROR("state", "command name/path is missing\n");
        return -1;
    }
    return 0;
}

static int __parse_commands(json_t* commands, struct served_application* application)
{
    size_t cmdsCount = json_array_size(commands);
    if (cmdsCount == 0) {
        return 0;
    }

    application->commands = calloc(cmdsCount, sizeof(struct served_command));
    if (application->commands == NULL) {
        return -1;
    }

    application->commands_count = (int)json_array_size(commands);
    for (int i = 0; i < application->commands_count; i++) {
        json_t* command = json_array_get(commands, i);
        int     status  = __parse_command(command, &application->commands[i]);
        if (status != 0) {
            VLOG_ERROR("state", "failed to parse command index %i in application %s\n", i, application->name);
            return status;
        }
    }
    return 0;
}

static int __parse_app(json_t* app, struct served_application* application)
{
    json_t* member;
    json_t* commands;

    application->name      = __get_string_safe(app, "name");
    application->publisher = __get_string_safe(app, "publisher");
    application->package   = __get_string_safe(app, "package");
    application->major     = (int)json_integer_value(json_object_get(app, "major"));
    application->minor     = (int)json_integer_value(json_object_get(app, "minor"));
    application->patch     = (int)json_integer_value(json_object_get(app, "patch"));
    application->revision  = (int)json_integer_value(json_object_get(app, "revision"));

    commands = json_object_get(app, "commands");
    if (commands) {
        return __parse_commands(commands, application);
    }
    return 0;
}

static int __parse_apps(json_t* apps, struct __state* state)
{
    size_t appsCount = json_array_size(apps);

    if (appsCount == 0) {
        return 0;
    }

    state->applications = (struct served_application**)calloc(appsCount, sizeof(struct served_application*));
    if (state->applications == NULL) {
        return -1;
    }

    state->application_count = (int)appsCount;
    for (size_t i = 0; i < appsCount; i++) {
        json_t* app = json_array_get(apps, i);
        int     status;

        state->applications[i] = served_application_new();
        if (state->applications[i] == NULL) {
            return -1;
        }

        status = __parse_app(app, state->applications[i]);
        if (status != 0) {
            VLOG_ERROR("state", "failed to parse application index %i from state.json\n", i);
            return status;
        }
    }

    return 0;
}

static int __parse_state(const char* content, struct __state** stateOut)
{
    struct __state* state;
    json_error_t    error;
    json_t*         root;
    json_t*         apps;
    int             status = -1;
    VLOG_DEBUG("state", "__parse_state()\n");

    state = __state_new();
    if (state == NULL) {
        return -1;
    }

    if (content == NULL) {
        *stateOut = state;
        return 0;
    }

    root = json_loads(content, 0, &error);
    if (!root) {
        status = 0;
        goto exit;
    }

    apps = json_object_get(root, "applications");
    if (apps) {
        status = __parse_apps(apps, state);
    }

exit:
    *stateOut = state;
    return status;
}

static int __ensure_file(const char* path, char** jsonOut)
{
    FILE* file;
    long  size;
    char* json = NULL;

    file = fopen(path, "r+");
    if (file == NULL) {
        file = fopen(path, "w+");
        if (file == NULL) {
            return -1;
        }
    }

    fseek(file, 0, SEEK_END);
    size = ftell(file);
    fseek(file, 0, SEEK_SET);

    if (size) {
        size_t bytesRead;

        json = (char*)malloc(size + 1); // sz?!
        if (!json) {
            fclose(file);
            return -1;
        }
        memset(json, 0, size + 1);
        bytesRead = fread(json, 1, size, file);
        if (bytesRead != size) {
            VLOG_ERROR("state", "could only read %zu out of %zu bytes from state file\n", bytesRead, size);
            fclose(file);
            return -1;
        }
    }

    fclose(file);
    *jsonOut = json;
    return 0;
}

static const char* __get_state_path(void)
{
    char* path;
    int   status;

    path = malloc(128);
    if (path == NULL) {
        return NULL;
    }

    sprintf(&path[0], "/var/chef/state.json");
    return path;
}

int served_state_load(void)
{
    int         status;
    char*       json;
    const char* filePath;
    VLOG_DEBUG("state", "served_state_load()\n");

    filePath = __get_state_path();
    if (filePath == NULL) {
        VLOG_ERROR("state", "failed to retrieve the path where the state is stored\n");
        return -1;
    }

    status = __ensure_file(filePath, &json);
    free((void*)filePath);
    if (status) {
        VLOG_ERROR("state", "failed to load state from %s\n", filePath);
        return -1;
    }

    status = __parse_state(json, &g_state);
    free(json);
    if (status) {
        VLOG_ERROR("state", "failed to parse the state, file corrupt??\n");
    }
    return status;
}

static int __serialize_command(struct served_command* command, json_t** jsonOut)
{
    json_t* json = json_object();
    if (json == NULL) {
        return -1;
    }

    json_object_set_new(json, "name", json_string(command->name));
    json_object_set_new(json, "path", json_string(command->path));
    json_object_set_new(json, "args", json_string(command->arguments));
    json_object_set_new(json, "type", json_integer(command->type));

    *jsonOut = json;
    return 0;
}

static int __serialize_application(struct served_application* application, json_t** jsonOut)
{
    json_t* json = json_object();
    if (!json) {
        return -1;
    }

    json_object_set_new(json, "name", json_string(application->name));
    json_object_set_new(json, "publisher", json_string(application->publisher));
    json_object_set_new(json, "package", json_string(application->package));
    json_object_set_new(json, "major", json_integer(application->major));
    json_object_set_new(json, "minor", json_integer(application->minor));
    json_object_set_new(json, "patch", json_integer(application->patch));
    json_object_set_new(json, "revision", json_integer(application->revision));

    json_t* commands = json_array();
    if (!commands) {
        json_decref(json);
        return -1;
    }

    for (int i = 0; i < application->commands_count; i++) {
        json_t* cmd;
        int     status;

        status = __serialize_command(&application->commands[i], &cmd);
        if (status != 0) {
            VLOG_ERROR("state", "failed to serialize command %s\n", application->commands[i].name);
            json_decref(json);
            json_decref(commands);
            return status;
        }
        json_array_append_new(commands, cmd);
    }

    json_object_set_new(json, "commands", commands);
    *jsonOut = json;
    return 0;
}

static int __serialize_state(struct __state* state, json_t** jsonOut)
{
    json_t* root;
    json_t* apps;
    
    root = json_object();
    if (!root) {
        return -1;
    }

    apps = json_array();
    if (!apps) {
        json_decref(root);
        return -1;
    }

    for (int i = 0; i < state->application_count; i++) {
        json_t* app;
        int     status;

        if (state->applications[i] != NULL) {
            status = __serialize_application(state->applications[i], &app);
            if (status != 0) {
                VLOG_ERROR("state", "failed to serialize application %s\n", state->applications[i]->name);
                json_decref(root);
                return status;
            }
            json_array_append_new(apps, app);
        }
    }

    json_object_set_new(root, "applications", apps);
    *jsonOut = root;
    return 0;
}

int served_state_save(void)
{
    json_t*     root;
    int         status;
    const char* filePath;
    VLOG_DEBUG("state", "served_state_save()\n");

    filePath = __get_state_path();
    if (filePath == NULL) {
        VLOG_ERROR("state", "failed to retrieve the path where the state is stored\n");
        return -1;
    }

    status = __serialize_state(g_state, &root);
    if (status) {
        VLOG_ERROR("state", "failed to serialize state to json\n");
        free((void*)filePath);
        return -1;
    }

    status = json_dump_file(root, filePath, JSON_INDENT(2));
    if (status) {
        VLOG_ERROR("state", "failed to write state to disk\n");
    }
    free((void*)filePath);
    json_decref(root);
    
    __state_destroy(g_state);
    g_state = NULL;

    return status;
}

int served_state_lock(void)
{
    // TODO
    return 0;
}

int served_state_unlock(void)
{
    // TODO
    return 0;
}

int served_state_get_applications(struct served_application*** applicationsOut, int* applicationsCount)
{
    if (g_state == NULL) {
        errno = ENOSYS;
        return -1;
    }

    *applicationsOut   = g_state->applications;
    *applicationsCount = g_state->application_count;
    return 0;
}

int served_state_add_application(struct served_application* application)
{
    struct served_application** applications;

    if (application == NULL) {
        errno = EINVAL;
        return -1;
    }

    applications = (struct served_application**)realloc(g_state->applications,
            (g_state->application_count + 1) * sizeof(struct served_application*));
    if (applications == NULL) {
        return -1;
    }

    applications[g_state->application_count] = application;
    g_state->applications = applications;
    g_state->application_count++;
    return 0;
}

int served_state_remove_application(struct served_application* application)
{
    struct served_application** applications;
    int                         i, j;

    if (application == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (g_state->application_count == 1) {
        free(g_state->applications);
        g_state->application_count = 0;
        g_state->applications      = NULL;
        return 0;
    }

    applications = (struct served_application**)malloc(
            (g_state->application_count - 1) * sizeof(struct served_application*));
    if (applications == NULL) {
        return -1;
    }

    for (i = 0, j = 0; i < g_state->application_count; i++) {
        if (g_state->applications[i] != application) {
            if (j == g_state->application_count - 1) {
                // someone lied to us, the target was not in here
                free(applications);
                errno = ENOENT;
                return -1;
            }
            applications[j++] = g_state->applications[i];
        }
    }

    free(g_state->applications);
    g_state->application_count--;
    g_state->applications = applications;
    return 0;
}
