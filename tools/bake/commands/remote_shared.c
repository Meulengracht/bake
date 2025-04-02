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

#include <vlog.h>

#include "chef_waiterd_service_client.h"

#include "commands.h"

struct __build {
    struct list_item              list_header;
    struct gracht_message_context msg_storage;
    int                           log_index;
    enum chef_build_status        last_status;
    char                          id[64];
    char                          arch[16];
};

static enum chef_build_architecture __arch_string_to_build_arch(const char* arch)
{
    if (strcmp(arch, "i386") == 0) {
        return CHEF_BUILD_ARCHITECTURE_X86;
    } else if (strcmp(arch, "amd64") == 0) {
        return CHEF_BUILD_ARCHITECTURE_X64;
    } else if (strcmp(arch, "armhf") == 0) {
        return CHEF_BUILD_ARCHITECTURE_ARMHF;
    } else if (strcmp(arch, "arm64") == 0) {
        return CHEF_BUILD_ARCHITECTURE_ARM64;
    } else if (strcmp(arch, "riscv64") == 0) {
        return CHEF_BUILD_ARCHITECTURE_RISCV64;
    }
    return CHEF_BUILD_ARCHITECTURE_X86;
}

static const char* build_arch_to_arch_string(enum chef_build_architecture arch)
{
    switch (arch) {
        case CHEF_BUILD_ARCHITECTURE_X86: return "i386";
        case CHEF_BUILD_ARCHITECTURE_X64: return "amd64";
        case CHEF_BUILD_ARCHITECTURE_ARMHF: return "armhf";
        case CHEF_BUILD_ARCHITECTURE_ARM64: return "arm64";
        case CHEF_BUILD_ARCHITECTURE_RISCV64: return "riscv64";
    }
    return "unknown";
}

static int __add_build(const char* arch, const char* id, int index, struct list* list)
{
    struct __build* item = calloc(1, sizeof(struct __build));
    if (item == NULL) {
        return -1;
    }
    item->log_index = index;
    strncpy(&item->id[0], id, sizeof(item->id));
    if (arch != NULL) {
        strncpy(&item->arch[0], arch, sizeof(item->arch));
    }
    list_add(list, &item->list_header);
    return 0;
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

        status = gracht_client_await_multiple(client, &msgs[0], builds->count, GRACHT_AWAIT_ALL);
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

            if (build->last_status == resp.status) {
                continue;
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
                } break;
                case CHEF_BUILD_STATUS_PACKING: {
                    VLOG_TRACE("remote", "build has completed, and is being packed\n");
                } break;
                case CHEF_BUILD_STATUS_DONE: {
                    VLOG_TRACE("remote", "build has completed\n");
                    vlog_content_set_status(VLOG_CONTENT_STATUS_DONE);
                    buildsCompleted++;
                } break;
                case CHEF_BUILD_STATUS_FAILED: {
                    VLOG_TRACE("remote", "build failed\n");
                    vlog_content_set_status(VLOG_CONTENT_STATUS_FAILED);
                    buildsCompleted++;
                } break;

                build->last_status = resp.status;
            }
        }

        // wait a little before we update status
        platform_sleep(5 * 1000);
    }
    return 0;
}
