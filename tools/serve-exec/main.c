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

#include <chef/api/package.h>
#include <chef/client.h>
#include <errno.h>
#include <gracht/client.h>
#include <chef/platform.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "chef_served_service_client.h"

extern int __chef_client_initialize(gracht_client_t** clientOut);

static char* __build_args(int argc, char** argv, const char* additionalArgs)
{
    char*  argumentString;
    char*  argumentItr;
    size_t totalLength = 0;

    argumentString = (char*)malloc(4096);
    if (argumentString == NULL) {
        errno = ENOMEM;
        return NULL;
    }
    memset(argumentString, 0, 4096);
    argumentItr = argumentString;

    // make sure the arguments from the command come first, this is to make sure
    // if a command is invoking a subcommand, this must be supported.
    if (additionalArgs != NULL) {
        totalLength = strlen(additionalArgs);
        if (totalLength) {
            strcpy(argumentString, additionalArgs);
            argumentItr += totalLength;
        }
    }

    // copy arguments into buffer, starting from argv[1] as
    // we do not copy the program path
    for (int i = 1; i < argc; i++) {
        size_t valueLength = strlen(argv[i]);
        if (valueLength > 0 && (totalLength + valueLength + 2) < 4096) {
            strcpy(argumentItr, argv[i]);

            totalLength += valueLength;
            argumentItr += valueLength;
            if (i < (argc - 1)) {
                *argumentItr = ' ';
                argumentItr++;
            }
        }
    }
    return argumentString;
}

static int __spawn_command(struct chef_served_command* command, int argc, char** argv, char** envp)
{
    char* arguments;
    int   status;

    arguments = __build_args(argc, argv, command->arguments);
    if (arguments == NULL) {
        return -1;
    }

    status = platform_spawn(command->path, arguments, (const char* const*)envp, command->data_path);
    free(arguments);
    return status;
}

static void __cleanup_command(struct chef_served_command* command)
{
    free((void*)command->path);
    free((void*)command->arguments);
    free((void*)command->data_path);
}

int main(int argc, char** argv, char** envp)
{
    struct chef_served_command command;
    gracht_client_t*           client;
    int                        status;

    status = chefclient_initialize();
    if (status != 0) {
        fprintf(stderr, "failed to initialize chef client\n");
        return -1;
    }
    atexit(chefclient_cleanup);

    // what we essentially do is redirect everything based on the application
    // path passed in argv[0]. This will tell us exactly which application is currently
    // executing.

    // So we use argv[0] to retrieve command information, together with application information
    // and then setup the environment for the command, and pass argv[1+] to it
    status = __chef_client_initialize(&client);
    if (status != 0) {
        printf("failed to initialize client: %s\n", strerror(status));
        return status;
    }

    chef_served_get_command(client, NULL, argv[0]);
    gracht_client_wait_message(client, NULL, GRACHT_MESSAGE_BLOCK);
    chef_served_get_command_result(client, NULL, &command);

    status = __spawn_command(&command, argc, argv, envp);
    __cleanup_command(&command);
    gracht_client_shutdown(client);
    return status;
}