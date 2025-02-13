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

#include <chef/platform.h>
#include "chef_waiterd_cook_service_client.h"
#include <server.h>
#include <vlog.h>

static const char* __architecture(enum chef_build_architecture arch)
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
    return CHEF_ARCHITECTURE_STR;
}

void chef_waiterd_cook_event_update_request_invocation(gracht_client_t* client, const struct chef_cook_update_request* request)
{
    struct gracht_message_context msg;
    struct cookd_status           info;
    int                           status;
    VLOG_DEBUG("api", "chef_waiterd_cook_event_update_request_invocation()\n");
    
    cookd_server_status(&info);

    status = chef_waiterd_cook_update(client, &msg, &(struct chef_cook_update_event) {
        .queue_size = info.queue_size
    });
    if (status) {
        VLOG_ERROR("api", "failed to update the waiterd daemon about current status\n");
    }
}

void chef_waiterd_cook_event_build_request_invocation(gracht_client_t* client, const char* id, const struct chef_waiter_build_request* request)
{
    struct gracht_message_context msg;
    int                           status;
    enum chef_build_status        buildStatus;
    VLOG_DEBUG("api", "chef_waiterd_cook_event_build_request_invocation(id=%s)\n", id);

    status = cookd_server_queue_build(id, &(struct cookd_build_options) {
        .architecture = __architecture(request->arch),
        .platform = request->platform,
        .url = request->url,
        .recipe_path = request->recipe
    });
    if (status) {
        VLOG_ERROR("api", "failed to queue build id %s\n", id);
        buildStatus = CHEF_BUILD_STATUS_FAILED;
    } else {
        buildStatus = CHEF_BUILD_STATUS_QUEUED;
    }

    status = chef_waiterd_cook_status(client, &msg, &(struct chef_cook_build_event) {
        .id = (char*)id,
        .status = buildStatus
    });
    if (status) {
        VLOG_ERROR("api", "failed to update the waiterd daemon for build id %s\n", id);
    }
}
