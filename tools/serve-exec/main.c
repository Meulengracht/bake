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

#include <chef/containerv.h>
#include <errno.h>
#include <gracht/client.h>
#include <chef/platform.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "chef_served_service_client.h"

extern int __chef_client_initialize(gracht_client_t** clientOut);

static char** __rebuild_args(int argc, char** argv, const char* arg0, char* additionalArgs)
{
    char** result;
    char** additional = NULL;
    int    count = argc;
    int    i;

    // also count the additional args
    if (additionalArgs != NULL) {
        int additionalCount;
        additional = strargv(additionalArgs, NULL, &additionalCount);
        if (additional == NULL) {
            return NULL;
        }
        count += additionalCount;
    }

    // Must be zero terminated
    result = calloc(count + 1, sizeof(char*));
    if (result == NULL) {
        free(additional);
        return NULL;
    }

    i = 0;

    // install custom arg0 if provided
    if (arg0 != NULL) {
        result[i++] = (char*)arg0;
    }

    // transfer provided argv array (maybe including 0)
    for (; i < argc; i++) {
        result[i] = argv[i];
    }

    // set the additional ones
    if (additional != NULL) {
        for (int j = 0; additional[j] != NULL; j++) {
            result[i++] = additional[j];
        }
        strargv_free(additional);
    }
    return result;
}

static int __spawn_command(struct chef_served_command* command, int argc, char** argv, char** envp)
{
    char** rebuildArgv;
    int    status;

    if (command->path == NULL || strlen(command->path) == 0) {
        fprintf(stderr, "serve-exec: cannot be invoked directly\n");
        return -1;
    }

    rebuildArgv = __rebuild_args(argc, argv, NULL, command->arguments);
    if (rebuildArgv == NULL) {
        fprintf(stderr, "serve-exec: failed to build command arguments\n");
        return -1;
    }

    status = containerv_join(command->container_control_path);
    if (status) {
        fprintf(stderr, "serve-exec: failed to prepare environment\n");
        return status;
    }

#if __linux__
    status = chdir(command->data_path);
    if (status) {
        fprintf(stderr, "serve-exec: failed to change directory to %s\n", command->data_path);
        return status;
    }

    status = execve(command->path, (char* const*)rebuildArgv, (char* const*)envp);
    if (status) {
        fprintf(stderr, "serve-exec: failed to execute %s\n", command->path);
    }
#endif
    free(rebuildArgv);
    return status;
}

int main(int argc, char** argv, char** envp)
{
    struct gracht_message_context context;
    struct chef_served_command    command;
    gracht_client_t*              client;
    int                           status;

    // what we essentially do is redirect everything based on the application
    // path passed in argv[0]. This will tell us exactly which application is currently
    // executing.
    chef_served_command_init(&command);

    // So we use argv[0] to retrieve command information, together with application information
    // and then setup the environment for the command, and pass argv[1+] to it
    status = __chef_client_initialize(&client);
    if (status != 0) {
        printf("failed to initialize client: %s\n", strerror(status));
        return status;
    }

    chef_served_get_command(client, &context, argv[0]);
    gracht_client_wait_message(client, &context, GRACHT_MESSAGE_BLOCK);
    chef_served_get_command_result(client, &context, &command);

    // close out the client before spawning command
    gracht_client_shutdown(client);

    status = __spawn_command(&command, argc, argv, envp);
    chef_served_command_destroy(&command);
    return status;
}
