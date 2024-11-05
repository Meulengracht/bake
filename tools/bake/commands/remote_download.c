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
#include <chef/client.h>
#include <chef/dirs.h>
#include <chef/list.h>
#include <chef/platform.h>
#include <chef/storage/download.h>
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
    printf("Usage: bake remote download --ids=<list-of-ids> [options]\n");
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

struct __build {
    struct list_item              list_header;
    struct gracht_message_context msg_storage;
    int                           log_index;
    char                          id[64];
    char                          arch[16];
    int                           success;
    char                          package_link[4096];
    char                          log_link[4096];
};

static const char* __parse_protocol_arch(enum chef_build_architecture arch)
{
    switch (arch) {
        case CHEF_BUILD_ARCHITECTURE_X86:
            return "i386";
        case CHEF_BUILD_ARCHITECTURE_X64:
            return "amd64";
        case CHEF_BUILD_ARCHITECTURE_ARMHF:
            return "armhf";
        case CHEF_BUILD_ARCHITECTURE_ARM64:
            return "arm64";
        case CHEF_BUILD_ARCHITECTURE_RISCV64:
            return "riscv64";
    }
    return "unknown";
}

static int __wait_for_builds(gracht_client_t* client, struct list* builds)
{
    int buildsCompleted = 0;
    while (buildsCompleted < builds->count) {
        struct gracht_message_context* msgs[6] = { NULL };
        struct list_item*              li;
        int                            status;
        int                            i;

        // iterate through each of the builds and setup
        i = 0;
        list_foreach(builds, li) {
            struct __build* build = (struct __build*)li;

            vlog_content_set_index(build->log_index);
            status = chef_waiterd_status(client, &build->msg_storage, &build->id[0]);
            if (status) {
                vlog_content_set_status(VLOG_CONTENT_STATUS_FAILED);
                return -1;
            }
            msgs[i++] = &build->msg_storage;
        }

        status = gracht_client_await_multiple(client, &msgs[0], i, GRACHT_AWAIT_ALL);
        if (status) {
            VLOG_ERROR("remote", "connection lost waiting for build status\n");
            return -1;
        }

        list_foreach(builds, li) {
            struct __build* build = (struct __build*)li;
            struct chef_waiter_status_response resp;

            vlog_content_set_index(build->log_index);
            status = chef_waiterd_status_result(client, &build->msg_storage, &resp);
            if (status) {
                vlog_content_set_status(VLOG_CONTENT_STATUS_FAILED);
                return -1;
            }

            // update arch
            if (build->arch[0] == '\0') {
                strcpy(&build->arch[0], __parse_protocol_arch(resp.arch));
                vlog_content_set_prefix(&build->arch[0]);
            }

            switch (resp.status) {
                case CHEF_BUILD_STATUS_UNKNOWN: {
                    // unknown means it hasn't started yet, so for now we do
                    // nothing, and do not change the current status
                } break;
                case CHEF_BUILD_STATUS_QUEUED: {
                    VLOG_TRACE("remote", "build is currently waiting to be serviced\n");
                } break;
                case CHEF_BUILD_STATUS_SOURCING: {
                    VLOG_TRACE("remote", "build is now sourcing\n");
                    vlog_content_set_status(VLOG_CONTENT_STATUS_WORKING);
                } break;
                case CHEF_BUILD_STATUS_BUILDING: {
                    VLOG_TRACE("remote", "build is in progress\n");
                    vlog_content_set_status(VLOG_CONTENT_STATUS_WORKING);
                } break;
                case CHEF_BUILD_STATUS_PACKING: {
                    VLOG_TRACE("remote", "build has completed, and is being packed\n");
                    vlog_content_set_status(VLOG_CONTENT_STATUS_WORKING);
                } break;
                case CHEF_BUILD_STATUS_DONE: {
                    VLOG_TRACE("remote", "build has completed\n");
                    vlog_content_set_status(VLOG_CONTENT_STATUS_DONE);
                    build->success = 1;
                    buildsCompleted++;
                } break;
                case CHEF_BUILD_STATUS_FAILED: {
                    VLOG_TRACE("remote", "build failed\n");
                    vlog_content_set_status(VLOG_CONTENT_STATUS_FAILED);
                    buildsCompleted++;
                } break;
            }
        }

        // wait a little before we update status
        if (buildsCompleted != builds->count) {
            platform_sleep(5 * 1000);
        }
    }
    return 0;
}

static int __discover_artifacts(gracht_client_t* client, struct list* builds, enum chef_artifact_type atype)
{
    struct gracht_message_context* msgs[6] = { NULL };
    struct list_item*              li;
    int                            status;
    int                            i;

    // iterate through each of the builds and setup
    i = 0;
    list_foreach(builds, li) {
        struct __build* build = (struct __build*)li;

        // if the build was not successful, then we don't query for the
        // package link
        if (build->success == 0 && atype == CHEF_ARTIFACT_TYPE_PACKAGE) {
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
        const char*     link;

        // if the build was not successful, then we don't query for the
        // package link
        if (build->success == 0 && atype == CHEF_ARTIFACT_TYPE_PACKAGE) {
            continue;
        }

        switch (atype) {
            case CHEF_ARTIFACT_TYPE_LOG:
                link = &build->log_link[0];
                break;
            case CHEF_ARTIFACT_TYPE_PACKAGE:
                link = &build->package_link[0];
                break;
        }

        vlog_content_set_index(build->log_index);
        status = chef_waiterd_artifact_result(client, &build->msg_storage, link, 4096);
        if (status) {
            vlog_content_set_status(VLOG_CONTENT_STATUS_FAILED);
            return -1;
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

        if (build->success) {
            VLOG_TRACE("remote", "downloading package...\n");
            status = chef_client_gen_download(&build->package_link[0], NULL);
            if (status) {
                VLOG_ERROR("remote", "failed to retrieve package\n");
                vlog_content_set_status(VLOG_CONTENT_STATUS_FAILED);
                continue;
            }
        }

        VLOG_TRACE("remote", "downloading logs...\n");
        status = chef_client_gen_download(&build->log_link[0], NULL);
        if (status) {
            VLOG_ERROR("remote", "failed to retrieve log\n");
            vlog_content_set_status(VLOG_CONTENT_STATUS_FAILED);
            continue;
        }
        
        VLOG_TRACE("remote", "artifacts has been retrieved!\n");
    }

    chefclient_cleanup();
    return 0;
}

static int __add_build(const char* id, struct list* builds)
{
    struct __build* item = calloc(1, sizeof(struct __build));
    if (item == NULL) {
        return -1;
    }
    strncpy(&item->id[0], id, sizeof(item->id));
    list_add(builds, &item->list_header);
    return 0;
}

static int __parse_build_ids(char** argv, int argc, int* i, struct list* builds)
{
    char* ids = __split_switch(argv, argc, i);
    if (ids == NULL || strlen(ids) == 0) {
        return -1;
    }

    // create a build for each
    char* startOfId = ids;
    char* pOfId = startOfId;
    while (!*pOfId) {
        if (*pOfId == ',') {
            *pOfId = '\0';
            if (__add_build(startOfId, builds)) {
                fprintf(stderr, "bake: failed to track build id: %s\n", startOfId);
                return -1;
            }
            startOfId = pOfId + 1;
        }
        pOfId++;
    }
    return __add_build(startOfId, builds);
}

int remote_download_main(int argc, char** argv, char** envp, struct bake_command_options* options)
{
    struct list       builds = { 0 };
    gracht_client_t*  client = NULL;
    struct list_item* li;
    int               status, i;

    // catch CTRL-C
    signal(SIGINT, __cleanup_systems);

    // handle individual commands
    for (int i = 1; i < argc; i++) {
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

    status = __wait_for_builds(client, &builds);
    if (status) {
        VLOG_ERROR("bake", "failed to get information about builds\n");
        goto cleanup;
    }

    status = __discover_artifacts(client, &builds, CHEF_ARTIFACT_TYPE_PACKAGE);
    if (status) {
        VLOG_ERROR("bake", "failed to get information about builds\n");
        goto cleanup;
    }

    status = __discover_artifacts(client, &builds, CHEF_ARTIFACT_TYPE_LOG);
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
    return status;
}
