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
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <vlog.h>

#include "remote_shared.c"

// We keep this global to allow for printing the way to resume the
// current build if we terminated abnormally.
static struct list g_builds = { 0 };
static int         g_skipPrint = 0;

static void __print_resume_help(void)
{
    struct list_item* li;
    int               i = 0;

    if (g_builds.count == 0 || g_skipPrint != 0) {
        return;
    }
    
    printf("Remote build was abnormally terminted, however this can be resumed\n");
    printf("To resume the build operation, use the following command-line:\n\n");
    printf("bake remote resume --ids=");
    list_foreach(&g_builds, li) {
        struct __build* build = (struct __build*)li;
        if (i > 0) {
            printf(",%s", &build->id[0]);
        } else {
            printf("%s", &build->id[0]);
        }
        i++;
    }
    printf("\n");
}

static void __print_download_help(void)
{
    struct list_item* li;
    int               i = 0;

    if (g_builds.count == 0) {
        return;
    }
    
    printf("For both successful and failed builds, build logs are available\n");
    printf("To download build artifacts, use the following command-line:\n\n");
    printf("bake remote download {artifact,log} --ids=");
    list_foreach(&g_builds, li) {
        struct __build* build = (struct __build*)li;
        if (i > 0) {
            printf(",%s", &build->id[0]);
        } else {
            printf("%s", &build->id[0]);
        }
        i++;
    }
    printf("\n");
}

static void __print_help(void)
{
    printf("Usage: bake remote build RECIPE [options]\n");
    printf("  Remote can be used to execute recipes remotely for a configured\n");
    printf("  build-server. It will connect to the configured waiterd instance in\n");
    printf("  the configuration file (bake.json)\n");
    printf("  If the connection is severed between the bake instance and the waiterd\n");
    printf("  instance, the build can be resumed from the bake instance by invoking\n");
    printf("  'bake remote resume --ids={list,of,ids}'\n\n");
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

    // inform user on how to proceed
    __print_resume_help();

    // Use _Exit to not run atexit/atquickexit
    _Exit(-sig);
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
    snprintf(&tmp[0], sizeof(tmp), "remote build%s", waiterdAddress);
    return platform_strdup(&tmp[0]);
}

static int __queue_builds(int logIndexStart, gracht_client_t* client, const char* imageUrl, struct list* builds, struct bake_command_options* options)
{
    struct {
        struct gracht_message_context msg;
        const char*                   arch;
        int                           log_index;
    } storage[6] = { 0 };
    struct gracht_message_context* msgs[6] = { NULL };
    struct list_item*              li;
    int                            logIndex;
    int                            msgIndex = 0;
    int                            status;

    logIndex = logIndexStart;
    list_foreach(&options->architectures, li) {
        const char* arch = ((struct list_item_string*)li)->value;
        vlog_content_set_index(logIndex);
        
        VLOG_TRACE("remote", "requesting build...\n");
        status = chef_waiterd_build(client, &storage[msgIndex].msg, 
            &(struct chef_waiter_build_request) {
                .arch = __arch_string_to_build_arch(arch),
                .platform = (char*)options->platform,
                .url = (char*)imageUrl,
                .recipe = (char*)options->recipe_path
            }
        );
        if (status) {
            vlog_content_set_status(VLOG_CONTENT_STATUS_FAILED);
        } else {
            storage[msgIndex].arch = arch;
            storage[msgIndex].log_index = logIndex;
            msgs[msgIndex] = &storage[msgIndex].msg;
            msgIndex++;
        }
        logIndex++;
    }
    
    status = gracht_client_await_multiple(client, &msgs[0], msgIndex, GRACHT_AWAIT_ALL);
    if (status) {
        VLOG_ERROR("remote", "connection lost waiting for builds\n");
        return -1;
    }

    for (int i = 0; i < msgIndex; i++) {
        char                   id[64] = { 0 };
        enum chef_queue_status qstatus;

        vlog_content_set_index(storage[i].log_index);
        status = chef_waiterd_build_result(client, &storage[i].msg, &qstatus, &id[0], sizeof(id));
        if (status) {
            vlog_content_set_status(VLOG_CONTENT_STATUS_FAILED);
            return -1;
        }

        switch (qstatus) {
            case CHEF_QUEUE_STATUS_NO_COOK_FOR_ARCHITECTURE: {
                VLOG_ERROR("remote", "architecture unsupported\n", &id[0]);
                vlog_content_set_status(VLOG_CONTENT_STATUS_FAILED);
            } break;
            case CHEF_QUEUE_STATUS_INTERNAL_ERROR: {
                VLOG_ERROR("remote", "internal build error\n");
                vlog_content_set_status(VLOG_CONTENT_STATUS_FAILED);
            } break;
            case CHEF_QUEUE_STATUS_SUCCESS: {
                status = __add_build(storage[i].arch, &id[0], storage[i].log_index, builds);
                if (status) {
                    VLOG_ERROR("remote", "failed to track build id: %s\n", &id[0]);
                    break;
                }
                VLOG_TRACE("remote", "build id: %s\n", &id[0]);
            }
        }
    }
    return 0;
}

int remote_build_main(int argc, char** argv, char** envp, struct bake_command_options* options)
{
    gracht_client_t*  client = NULL;
    struct list_item* li;
    char*             imagePath = NULL;
    char*             dlUrl = NULL;
    char*             header;
    char*             footer;
    int               status;
    int               i;

    // catch CTRL-C
    signal(SIGINT, __cleanup_systems);

    // skip ahead of 'build'
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "build")) {
            i++;
            break;
        }
    }

    // handle build options that needs to be proxied
    for (; i < argc; i++) {
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

    header = __format_header(options->recipe->project.name, options->platform, "*");
    footer = __format_footer("");
    if (footer == NULL) {
        fprintf(stderr, "bake: failed to allocate memory for build footer\n");
        return -1;
    }

    // setup the build log box
    vlog_start(stdout , header, footer, 3 + options->architectures.count);

    // 0+1 are informational
    vlog_content_set_index(0);
    vlog_content_set_prefix("connect");

    vlog_content_set_index(1);
    vlog_content_set_prefix("prepare");
    vlog_content_set_status(VLOG_CONTENT_STATUS_WAITING);

    vlog_content_set_index(2);
    vlog_content_set_prefix("");

    i = 3;
    list_foreach(&options->architectures, li) {
        vlog_content_set_index(i++);
        vlog_content_set_prefix(((struct list_item_string*)li)->value);
        vlog_content_set_status(VLOG_CONTENT_STATUS_WAITING);
    }

    // The first step is connection
    vlog_content_set_index(0);
    vlog_content_set_status(VLOG_CONTENT_STATUS_WORKING);
    
    VLOG_TRACE("bake", "initializing network client\n");
    status = chefclient_initialize();
    if (status != 0) {
        VLOG_ERROR("bake", "remote_upload: failed to initialize chef client\n");
        return -1;
    }

    VLOG_TRACE("bake", "connecting to waiterd\n");
    status = remote_client_create(&client);
    if (status) {
        VLOG_ERROR("bake", "failed to connect to the configured waiterd instance\n");
        goto cleanup;
    }
    VLOG_TRACE("bake", "connected\n");

    // first step done
    vlog_content_set_status(VLOG_CONTENT_STATUS_DONE);

    // prepare the source for sending
    vlog_content_set_index(1);
    vlog_content_set_status(VLOG_CONTENT_STATUS_WORKING);

    VLOG_TRACE("bake", "packing source code for delivery\n");
    status = remote_pack(options->cwd, (const char* const*)envp, &imagePath);
    if (status) {
        goto cleanup;
    }

    VLOG_TRACE("bake", "uploading source code image\n");
    status = remote_upload(imagePath, &dlUrl);
    if (status) {
        goto cleanup;
    }

    VLOG_TRACE("bake", "source has been uploaded\n");
    vlog_content_set_status(VLOG_CONTENT_STATUS_DONE);
 
    // initiate all the build calls
    status = __queue_builds(3, client, dlUrl, &g_builds, options);
    if (status) {
        goto cleanup;
    }

    // register resume helper
    atexit(__print_resume_help);
    
    // wait a little before we update status
    platform_sleep(1000);

    // poll queued builds
    status = __wait_for_builds(client, &g_builds);

cleanup:
    chefclient_cleanup();
    gracht_client_shutdown(client);
    if (status) {
        vlog_content_set_status(VLOG_CONTENT_STATUS_FAILED);
    }
    
    // end the view now
    vlog_refresh(stdout);
    vlog_end();

    // print the guide on how to download artifacts
    __print_download_help();
    
    // now cleanup stuff
    __build_list_delete(&g_builds);
    free(imagePath);
    free(dlUrl);
    g_skipPrint = 1;
    return status;
}
