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
#include <state.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <jansson.h>

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

static int __parse_command(json_t* cmd, struct served_command* command)
{
    json_t* name;
    json_t* path;
    json_t* args;
    json_t* type;

    name = json_object_get(cmd, "name");
    if (name) {
        command->name = strdup(json_string_value(name));
    }
    
    path = json_object_get(cmd, "path");
    if (path) {
        command->path = strdup(json_string_value(path));
    }

    args = json_object_get(cmd, "args");
    if (args) {
        command->arguments = strdup(json_string_value(args));
    }

    type = json_object_get(cmd, "type");
    if (type) {
        command->type = json_integer_value(type);
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
            return status;
        }
    }
    return 0;
}

static int __parse_app(json_t* app, struct served_application* application)
{
    json_t* name;
    json_t* commands;

    name = json_object_get(app, "name");
    if (name) {
        application->name = strdup(json_string_value(name));
    }

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
    int             status;

    state = __state_new();
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
    return 0;
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
            fprintf(stderr, "__load_file: failed to read file: %s\n", strerror(errno));
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

    path = malloc(PATH_MAX);
    if (path == NULL) {
        return NULL;
    }

    status = platform_getuserdir(path, PATH_MAX);
    if (status != 0) {
        free(path);
        return NULL;
    }

    strcat(path, CHEF_PATH_SEPARATOR_S ".chef" CHEF_PATH_SEPARATOR_S "state.json");
    return path;
}

int served_state_load(void)
{
    int         status;
    char*       json;
    const char* filePath;

    filePath = __get_state_path();
    if (filePath == NULL) {
        return -1;
    }

    status = __ensure_file(filePath, &json);
    free((void*)filePath);
    if (status) {
        fprintf(stderr, "served_state_load: failed to load %s\n", filePath);
        return -1;
    }

    status = __parse_state(json, &g_state);
    free(json);
    if (status) {
        fprintf(stderr, "served_state_load: failed to parse the state, file corrupt??\n");
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

    filePath = __get_state_path();
    if (filePath == NULL) {
        return -1;
    }

    status = __serialize_state(g_state, &root);
    if (status) {
        free((void*)filePath);
        return -1;
    }

    status = json_dump_file(root, filePath, JSON_INDENT(2));
    free((void*)filePath);
    json_decref(root);
    
    __state_destroy(g_state);
    g_state = NULL;

    return status;
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
