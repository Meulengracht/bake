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
#include <chef/list.h>
#include <chef/platform.h>
#include <chef/remote.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <vlog.h>

#include "chef_waiterd_service_client.h"

#include "commands.h"

static void __print_help(void)
{
    printf("Usage: bake remote <command> RECIPE [options]\n");
    printf("  Remote can be used to execute recipes remotely for a configured\n");
    printf("  build-server. It will connect to the configured waiterd instance in\n");
    printf("  the configuration file (bake.json)\n");
    printf("  If the connection is severed between the bake instance and the waiterd\n");
    printf("  instance, the build can be resumed from the bake instance by invoking\n");
    printf("  'bake remote resume <ID>'\n\n");
    printf("  To see a full list of supported options for building, please execute\n");
    printf("  'bake build --help'\n\n");
    printf("\n");
    printf("Options:\n");
    printf("  --version\n");
    printf("      Print the version of bake\n");
    printf("  -h,  --help\n");
    printf("      Shows this help message\n");
}

static void __cleanup_systems(int sig)
{
    // printing as a part of a signal handler is not safe
    // but we live dangerously
    vlog_content_set_status(VLOG_CONTENT_STATUS_FAILED);
    vlog_end();

    // cleanup logging
    vlog_cleanup();

    // Do a quick exit, which is recommended to do in signal handlers
    // and use the signal as the exit code
    _Exit(-sig);
}

static char* __add_build_log(void)
{
    char* path;
    FILE* stream = chef_dirs_contemporary_file("bake-build", ".log", &path);
    if (stream == NULL) {
        return NULL;
    }

    vlog_add_output(stream, 1);
    vlog_set_output_level(stream, VLOG_LEVEL_DEBUG);
    return path;
}

static char* __format_header(const char* name, const char* platform, const char* arch)
{
    char tmp[512];
    snprintf(&tmp[0], sizeof(tmp), "%s (%s, %s)", name, platform, arch);
    return platform_strdup(&tmp[0]);
}

static char* __format_footer(const char* waiterdAddress)
{
    char tmp[PATH_MAX];
    snprintf(&tmp[0], sizeof(tmp), "connected to: %s", waiterdAddress);
    return platform_strdup(&tmp[0]);
}

int remote_build_main(int argc, char** argv, char** envp, struct bake_command_options* options)
{
    struct gracht_message_context msg;
    gracht_client_t* client = NULL;
    char*            imagePath = NULL;
    char*            header;
    char*            footer;
    int              status;

    // catch CTRL-C
    signal(SIGINT, __cleanup_systems);

    // handle build options that needs to be proxied
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            __print_help();
            return 0;
        }
    }

    if (options->recipe == NULL) {
        fprintf(stderr, "bake: no recipe provided\n");
        __print_help();
        return -1;
    }

    header = __format_header(options->recipe->project.name, options->platform, options->architecture);
    footer = __format_footer("");
    if (footer == NULL) {
        fprintf(stderr, "bake: failed to allocate memory for build footer\n");
        return -1;
    }

    // setup the build log box
    vlog_start(stdout, header, footer, 6);

    // 0+1 are informational
    vlog_content_set_index(0);
    vlog_content_set_prefix("connect");

    vlog_content_set_index(1);
    vlog_content_set_prefix("prepare");
    vlog_content_set_status(VLOG_CONTENT_STATUS_WAITING);

    vlog_content_set_index(2);
    vlog_content_set_prefix("");

    // TODO: support multiple archs
    vlog_content_set_index(3);
    vlog_content_set_prefix(options->architecture);
    vlog_content_set_status(VLOG_CONTENT_STATUS_WAITING);

    // The first step is connection
    vlog_content_set_index(0);
    vlog_content_set_status(VLOG_CONTENT_STATUS_WORKING);
    
    VLOG_TRACE("bake", "connecting to waiterd\n");
    status = remote_client_create(&client);
    if (status) {
        VLOG_ERROR("bake", "failed to connect to the configured waiterd instance\n");
        goto cleanup;
    }

    // first step done
    vlog_content_set_status(VLOG_CONTENT_STATUS_DONE);

    // prepare the source for sending
    vlog_content_set_index(1);
    vlog_content_set_status(VLOG_CONTENT_STATUS_WORKING);
    status = remote_pack(options->cwd, envp, &imagePath);
    if (status) {
        goto cleanup;
    }

    vlog_content_set_status(VLOG_CONTENT_STATUS_DONE);

    // initiate all the build calls
    vlog_content_set_index(3);
    vlog_content_set_status(VLOG_CONTENT_STATUS_WORKING);

    // TODO: do in loop for each arch
    status = chef_waiterd_build(client, &msg, 
        &(struct chef_waiter_build_request) {
        
        }
    );
    gracht_client_await(client, &msg, GRACHT_MESSAGE_BLOCK);
    chef_waiterd_build_result(client, &msg, NULL, NULL, 0);

cleanup:
    gracht_client_shutdown(client);
    if (status) {
        vlog_content_set_status(VLOG_CONTENT_STATUS_FAILED);
    }
    vlog_refresh(stdout);
    vlog_end();
    free(imagePath);
    return status;
}
