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
#include <libplatform.h>
#include <state.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <jansson.h>

struct __state {
    struct served_application* applications;
    int                        application_count;
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

static int __parse_app(json_t* app, struct served_application* application)
{

}

static int __parse_apps(json_t* apps, struct __state* state)
{
    size_t appsCount = json_array_size(apps);

    if (appsCount == 0) {
        return 0;
    }

    state->applications = (struct served_application*)calloc(appsCount, sizeof(struct served_application));
    if (state->applications == NULL) {
        return -1;
    }

    state->application_count = (int)appsCount;
    for (size_t i = 0; i < appsCount; i++) {
        json_t* app    = json_array_get(apps, i);
        int     status = __parse_app(app, &state->applications[i]);
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

    apps = json_object_get(root, "apps");
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

    strcat(path, CHEF_PATH_SEPARATOR_S ".chef/state.json");
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

int served_state_save(void)
{


    return 0;
}
