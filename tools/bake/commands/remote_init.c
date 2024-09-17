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
 * Package System TODOs:
 * - api-keys
 * - pack deletion
 */
#define _GNU_SOURCE

#include <errno.h>
#include <chef/dirs.h>
#include <chef/platform.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chef-config.h"
#include "commands.h"

#define __DEFAULT_LOCAL_CONNECTION_STRING "unix:/run/chef/waiterd/api"

static void __print_help(void)
{
    printf("Usage: bake remote init [options]\n");
    printf("  Remote can be used to execute recipes remotely for a configured\n");
    printf("  build-server. It will connect to the configured waiterd instance in\n");
    printf("  the configuration file (bake.json)\n");
    printf("  If the connection is severed between the bake instance and the waiterd\n");
    printf("  instance, the build can be resumed from the bake instance by invoking\n");
    printf("  'bake remote resume <ID>'\n\n");
    printf("Options:\n");
    printf("  -l,  --local\n");
    printf("      Configures the default local connections for waiterd,\n");
    printf("      this will only work if waiterd runs on the same machine\n");
    printf("      with the same default setup\n");
    printf("  -h,  --help\n");
    printf("      Shows this help message\n");
}

static int __ask_yes_no_question(const char* question)
{
    char answer[3] = { 0 };
    printf("%s (default=no) [Y/n] ", question);
    if (fgets(answer, sizeof(answer), stdin) == NULL) {
        return 0;
    }
    return answer[0] == 'Y';
}

static char* __ask_question(const char* question, const char* defaultAnswer)
{
    char answer[512] = { 0 };
    printf("%s (default=%s) [Y/n] ", question, defaultAnswer);
    if (fgets(answer, sizeof(answer), stdin) == NULL) {
        return 0;
    }
    return platform_strdup(&answer[0]);
}

static int __validate_connection_string(const char* connectionString)
{

}

static int __write_configuration(const char* connectionString)
{
    printf("updating bake configuration\n");
}

static int __init_wizard(void)
{
    char* connectionString = NULL;
    int   status;
    
    printf("Welcome to the remote build initialization wizard!\n");
    printf("This will guide you through the neccessary setup to\n");
    printf("enable remote builds on your local machine.\n");
    printf("Before we get started, you must have a computer\n");
    printf("setup with the waiterd/cookd software, and have their\n");
    printf("connection strings ready.\n");
    printf("Examples:\n");
    printf(" - unix:/my/path\n");
    printf(" - inet4:192.6.4.1:9202\n");
    printf("\n");
    
    connectionString = __ask_question(
        "please enter the address of the waiterd daemon",
        __DEFAULT_LOCAL_CONNECTION_STRING
    );
    if (__validate_connection_string(connectionString)) {
        free(connectionString);
        return -1;
    }

    status = __write_configuration(connectionString);
    free(connectionString);
    return status;
}

static int __write_local_configuration(void)
{
    return __write_configuration(__DEFAULT_LOCAL_CONNECTION_STRING);
}

int remote_init_main(int argc, char** argv, char** envp, struct bake_command_options* options)
{
    int local = 0;
    int i;

    // handle arguments specifically for init
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "init")) {
            i++;
            break;
        }
    }

    if (i < argc) {
        if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            __print_help();
            return 0;
        }

        if (!strcmp(argv[i], "--version")) {
            printf("bake: version " PROJECT_VER "\n");
            return 0;
        }
    }

    if (local) {
        return __write_local_configuration();
    }
    return __init_wizard();
}
