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
#include <chef/platform.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static char** __rebuild_args(int argc, char** argv, const char* arg0, int argIndex)
{
    char** result;
    int    count = (argc - argIndex);
    int    i;

    // Must be zero terminated
    result = calloc(count + 1, sizeof(char*));
    if (result == NULL) {
        return NULL;
    }

    i = 0;

    // install custom arg0 if provided
    if (arg0 != NULL) {
        result[i++] = (char*)arg0;
    }

    // transfer provided argv array (maybe including 0)
    for (int j = argIndex; j < argc; j++, i++) {
        result[i] = argv[j];
    }
    return result;
}

static int __spawn_command(int argc, char** argv, char** envp, const char* containerName, const char* commandPath, const char* workingDirectory, int argIndex)
{
    char** rebuildArgv;
    int    status;

    if (commandPath == NULL || strlen(commandPath) == 0) {
        fprintf(stderr, "serve-exec: cannot be invoked directly\n");
        return -1;
    }

    rebuildArgv = __rebuild_args(argc, argv, NULL, argIndex);
    if (rebuildArgv == NULL) {
        fprintf(stderr, "serve-exec: failed to build command arguments\n");
        return -1;
    }

    status = containerv_join(containerName);
    if (status) {
        fprintf(stderr, "serve-exec: failed to prepare environment\n");
        return status;
    }

#if __linux__
    status = chdir(workingDirectory);
    if (status) {
        fprintf(stderr, "serve-exec: failed to change directory to %s\n", workingDirectory);
        return status;
    }

    status = execve(commandPath, (char* const*)rebuildArgv, (char* const*)envp);
    if (status) {
        fprintf(stderr, "serve-exec: failed to execute %s\n", commandPath);
    }
#endif
    free(rebuildArgv);
    return status;
}

// invoked as <serve-exec-path> --container <container-name> --path <path-inside-container> --wdir <working-directory> <arguments-for-internal-command>
int main(int argc, char** argv, char** envp)
{
    const char* containerName = NULL;
    const char* commandPath   = NULL;
    const char* workingDirectory = NULL;
    int         argIndex      = 1;
    int         status;

    while (argIndex < argc) {
        if (strcmp(argv[argIndex], "--container") == 0 && argIndex + 1 < argc) {
            containerName = argv[argIndex + 1];
            argIndex += 2;
        } else if (strcmp(argv[argIndex], "--path") == 0 && argIndex + 1 < argc) {
            commandPath = argv[argIndex + 1];
            argIndex += 2;
        } else if (strcmp(argv[argIndex], "--wdir") == 0 && argIndex + 1 < argc) {
            workingDirectory = argv[argIndex + 1];
            argIndex += 2;
        } else {
            break;
        }
    }

    // now assume the rest are for the command
    if (containerName == NULL || commandPath == NULL || workingDirectory == NULL) {
        fprintf(stderr, "serve-exec: missing required arguments --container, --path, and --wdir\n");
        return -1;
    }

    // what we essentially do is redirect everything based on the application
    // path passed in argv[0]. This will tell us exactly which application is currently
    // executing.

    // So we use argv[0] to retrieve command information, together with application information
    // and then setup the environment for the command, and pass argv[1+] to it

    status = __spawn_command(argc, argv, envp, containerName, commandPath, workingDirectory, argIndex);
    return status;
}
