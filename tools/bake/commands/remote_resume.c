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

#include "remote_shared.c"

static void __print_help(void)
{
    printf("Usage: bake remote resume --ids=<list-of-ids> [options]\n");
    printf("  If the connection is severed between the bake instance and the waiterd\n");
    printf("  instance, the build can be resumed from the bake instance by invoking\n");
    printf("  'bake remote resume <ID>'\n\n");
    printf("  To see a full list of supported options for building, please execute\n");
    printf("  'bake remote --help'\n\n");
    printf("Options:\n");
    printf("  -h,  --help\n");
    printf("      Shows this help message\n");
}

static void __cleanup_systems(int sig)
{
    // cleanup logging
    vlog_cleanup();

    // Do a quick exit, which is recommended to do in signal handlers
    // and use the signal as the exit code
    _Exit(-sig);
}

int remote_resume_main(int argc, char** argv, char** envp, struct bake_command_options* options)
{
    struct list       builds = { 0 };
    gracht_client_t*  client = NULL;
    struct list_item* li;
    int               status, i;
    struct vlog_step  step_connect;
    struct vlog_step  step_spacer;

    // catch CTRL-C
    signal(SIGINT, __cleanup_systems);

    // handle individual commands
    for (i = 1; i < argc; i++) {
        if (strncmp(argv[i], "--ids", 5) == 0) {
            status = __parse_build_ids(argv, argc, &i, &builds);
            if (status) {
                fprintf(stderr, "bake: cannot parse --ids, invalid options supplied\n");
                __print_help();
                return -1;
            }
        } else if (__cli_is_help_switch(argv[i])) {
            __print_help();
            return 0;
        } else {
            fprintf(stderr, "bake: unknown option %s\n", argv[i]);
            __print_help();
            return -1;
        }
    }

    if (builds.count == 0) {
        fprintf(stderr, "bake: --ids must be supplied to resume remote builds\n");
        __print_help();
        return -1;
    }

    // setup the build log box
    vlog_view_open(stdout, "remote build", "connected to: ");

    // 0+1 are informational
    vlog_step_open(&step_connect, "connect");
    vlog_step_open(&step_spacer, "");

    list_foreach(&builds, li) {
        struct __build* build = (struct __build*)li;

        __build_step_open(build);
        vlog_step_update(&build->step, VLOG_CONTENT_STATUS_WAITING, "resuming: %s", &build->id[0]);
    }

    // start by connecting
    vlog_step_update(&step_connect, VLOG_CONTENT_STATUS_WORKING, "connecting to waiterd");

    VLOG_TRACE("bake", "connecting to waiterd\n");
    status = remote_client_create(&client);
    if (status) {
        VLOG_ERROR("bake", "failed to connect to the configured waiterd instance\n");
        vlog_step_close(&step_connect, VLOG_CONTENT_STATUS_FAILED, "failed to connect to waiterd");
        goto cleanup;
    }
    vlog_step_close(&step_connect, VLOG_CONTENT_STATUS_DONE, "connected");

    status = __wait_for_builds(client, &builds);

cleanup:
    gracht_client_shutdown(client);
    vlog_flush();
    vlog_view_close();
    __build_list_delete(&builds);
    return status;
}
