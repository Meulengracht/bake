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
#include <chef/platform.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chef-config.h"
#include "commands.h"

extern int remote_init_main(int argc, char** argv, char** envp, struct bake_command_options* options);
extern int remote_build_main(int argc, char** argv, char** envp, struct bake_command_options* options);
extern int remote_resume_main(int argc, char** argv, char** envp, struct bake_command_options* options);
extern int remote_download_main(int argc, char** argv, char** envp, struct bake_command_options* options);

struct command_handler {
    char* name;
    int (*handler)(int argc, char** argv, char** envp, struct bake_command_options* options);
};

static struct command_handler g_commands[] = {
    { "init",   remote_init_main },
    { "build",  remote_build_main },
    { "resume", remote_resume_main },
    { "download", remote_download_main }
};

static void __print_help(void)
{
    printf("Usage: bake remote <command> RECIPE [options]\n");
    printf("  Remote can be used to execute recipes remotely for a configured\n");
    printf("  build-server. It will connect to the configured waiterd instance in\n");
    printf("  the configuration file (bake.json)\n");
    printf("  If the connection is severed between the bake instance and the waiterd\n");
    printf("  instance, the build can be resumed from the bake instance by invoking\n");
    printf("  'bake remote resume <ID>'\n\n");
    printf("  From any build id, two artifacts can be available. For both failed and\n");
    printf("  successful build, logs can be retrieved. From successful builds, build\n");
    printf("  artifacts can additionally be retrieved (packs)\n");
    printf("  'bake remote download {log, artifact} --ids=<ID>'\n\n");
    printf("  To see a full list of supported options for building, please execute\n");
    printf("  'bake build --help'\n\n");
    printf("Commands:\n");
    printf("  init     go through the configuration wizard\n");
    printf("  build    executes a recipe remotely\n");
    printf("  resume   resumes execution of a recipe running remotely\n");
    printf("  download retrieve any artifacts from a finished remote build\n");
    printf("\n");
    printf("Options:\n");
    printf("  --version\n");
    printf("      Print the version of bake\n");
    printf("  -h,  --help\n");
    printf("      Shows this help message\n");
}

static struct command_handler* __get_command(const char* command)
{
    for (int i = 0; i < sizeof(g_commands) / sizeof(struct command_handler); i++) {
        if (!strcmp(command, g_commands[i].name)) {
            return &g_commands[i];
        }
    }
    return NULL;
}

int remote_main(int argc, char** argv, char** envp, struct bake_command_options* options)
{
    struct command_handler* command = NULL;
    int                     i;

    // handle individual commands as well as --help and --version
    // locate the remote command on the cmdline
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "remote")) {
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

        command = __get_command(argv[i]);
    }

    if (command == NULL) {
        fprintf(stderr, "bake: command must be supplied for 'bake remote'\n");
        __print_help();
        return -1;
    }
    return command->handler(argc, argv, envp, options);
}
