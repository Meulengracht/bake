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
#define _GNU_SOURCE

#include <errno.h>
#include <chef/client.h>
#include <chef/dirs.h>
#include <chef/list.h>
#include <chef/platform.h>
#include <chef/remote.h>
#include <chef/storage/download.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <vlog.h>

#include "remote_shared.c"

static void __print_help(void)
{
    printf("Usage: bake remote download {log, artifact} --ids=<list-of-ids> [options]\n");
    printf("  From any build id, two artifacts can be available. For both failed and\n");
    printf("  successful build, logs can be retrieved. From successful builds, build\n");
    printf("  artifacts can additionally be retrieved (packs)\n");
    printf("  'bake remote download {log, artifact} --ids=<ID>'\n\n");
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

static int __discover_artifacts(gracht_client_t* client, struct list* builds, enum chef_artifact_type atype)
{
    struct gracht_message_context* msgs[6] = { NULL };
    struct list_item*              li;
    int                            status;
    int                            i;
    char                           linkBuffer[4096];

    // iterate through each of the builds and setup
    i = 0;
    list_foreach(builds, li) {
        struct __build* build = (struct __build*)li;

        // if the build was not successful, then we don't query for the
        // package link
        if (build->status != CHEF_BUILD_STATUS_DONE && atype == CHEF_ARTIFACT_TYPE_PACKAGE) {
            continue;
        }

        vlog_content_set_index(build->log_index);
        status = chef_waiterd_artifact(client, &build->msg_storage, &build->id[0], atype);
        if (status) {
            vlog_content_set_status(VLOG_CONTENT_STATUS_FAILED);
            return -1;
        }
        msgs[i++] = &build->msg_storage;
    }

    status = gracht_client_await_multiple(client, &msgs[0], i, GRACHT_AWAIT_ALL);
    if (status) {
        VLOG_ERROR("remote", "connection lost waiting for build artifact\n");
        return -1;
    }

    list_foreach(builds, li) {
        struct __build* build = (struct __build*)li;

        // if the build was not successful, then we don't query for the
        // package link, TODO: set skipped
        if (build->status != CHEF_BUILD_STATUS_DONE && atype == CHEF_ARTIFACT_TYPE_PACKAGE) {
            continue;
        }
        
        memset(&linkBuffer[0], 0, sizeof(linkBuffer));

        vlog_content_set_index(build->log_index);
        status = chef_waiterd_artifact_result(client, &build->msg_storage, &linkBuffer[0], sizeof(linkBuffer));
        if (status) {
            vlog_content_set_status(VLOG_CONTENT_STATUS_FAILED);
            return -1;
        }

        switch (atype) {
            case CHEF_ARTIFACT_TYPE_LOG:
                build->log_link = platform_strdup(&linkBuffer[0]);
                break;
            case CHEF_ARTIFACT_TYPE_PACKAGE:
                build->package_link = platform_strdup(&linkBuffer[0]);
                break;
        }
    }
    return 0;
}

static int __download_artifacts(struct list* builds)
{
    struct list_item* li;
    int               status;

    status = chefclient_initialize();
    if (status != 0) {
        VLOG_ERROR("remote", "__download_artifacts: failed to initialize chef client\n");
        return -1;
    }

    list_foreach(builds, li) {
        struct __build* build = (struct __build*)li;
        vlog_content_set_index(build->log_index);

        if (build->package_link != NULL) {
            VLOG_TRACE("remote", "downloading package...\n");
            status = chef_client_gen_download(&build->package_link[0], NULL);
            if (status) {
                VLOG_ERROR("remote", "failed to retrieve package\n");
                vlog_content_set_status(VLOG_CONTENT_STATUS_FAILED);
                continue;
            }
        }

        if (build->log_link != NULL) {
            VLOG_TRACE("remote", "downloading logs...\n");
            status = chef_client_gen_download(&build->log_link[0], NULL);
            if (status) {
                VLOG_ERROR("remote", "failed to retrieve log\n");
                vlog_content_set_status(VLOG_CONTENT_STATUS_FAILED);
                continue;
            }
        }
        
        VLOG_TRACE("remote", "artifacts has been retrieved!\n");
    }

    chefclient_cleanup();
    return 0;
}

int remote_download_main(int argc, char** argv, char** envp, struct bake_command_options* options)
{
    struct list             builds = { 0 };
    gracht_client_t*        client = NULL;
    struct list_item*       li;
    int                     status, i;
    enum chef_artifact_type atype;

    // catch CTRL-C
    signal(SIGINT, __cleanup_systems);

    // skip ahead of 'download'
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "download")) {
            i++;
            break;
        }
    }

    // next must be log or artifact
    if (strcmp(argv[i], "artifact") == 0) {
        atype = CHEF_ARTIFACT_TYPE_PACKAGE;
    } else if (strcmp(argv[i], "log") == 0) {
        atype = CHEF_ARTIFACT_TYPE_LOG;
    } else {
        fprintf(stderr, "bake: unsupported download type %s\n", argv[i]);
        __print_help();
        return -1;
    }
    i++;

    // handle individual options
    for (; i < argc; i++) {
        if (strncmp(argv[i], "--ids", 5) == 0) {
            status = __parse_build_ids(argv, argc, &i, &builds);
            if (status) {
                fprintf(stderr, "bake: cannot parse --ids, invalid options supplied\n");
                __print_help();
                return -1;
            }
        } else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            __print_help();
            return 0;
        }
    }

    if (builds.count == 0) {
        fprintf(stderr, "bake: --ids must be supplied to download build artifacts\n");
        __print_help();
        return -1;
    }

    // setup the build log box
    vlog_start(stdout , "downloading", "connected to: ", 2 + builds.count);

    // 0+1 are informational
    vlog_content_set_index(0);
    vlog_content_set_prefix("connect");

    vlog_content_set_index(1);
    vlog_content_set_prefix("");

    i = 2;
    list_foreach(&builds, li) {
        struct __build* build = (struct __build*)li;

        // attach a log index
        build->log_index = i;

        vlog_content_set_index(i++);
        vlog_content_set_prefix("");
        vlog_content_set_status(VLOG_CONTENT_STATUS_WAITING);
        VLOG_TRACE("remote", "syncing: %s...", &build->id[0]);
    }

    // start by connecting
    vlog_content_set_index(0);
    vlog_content_set_status(VLOG_CONTENT_STATUS_WORKING);

    VLOG_TRACE("bake", "connecting to waiterd\n");
    status = remote_client_create(&client);
    if (status) {
        VLOG_ERROR("bake", "failed to connect to the configured waiterd instance\n");
        goto cleanup;
    }

    status = __build_statuses(client, &builds);
    if (status) {
        VLOG_ERROR("bake", "failed to get information about builds\n");
        goto cleanup;
    }

    status = __discover_artifacts(client, &builds, atype);
    if (status) {
        VLOG_ERROR("bake", "failed to get information about builds\n");
        goto cleanup;
    }

    status = __download_artifacts(&builds);

cleanup:
    gracht_client_shutdown(client);
    if (status) {
        vlog_content_set_status(VLOG_CONTENT_STATUS_FAILED);
    }
    vlog_refresh(stdout);
    vlog_end();
    __build_list_delete(&builds);
    return status;
}
