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

#include <convert.h>
#include "chef_waiterd_service_server.h"
#include "chef_waiterd_cook_service_server.h"
#include <vlog.h>

static int __verify_build_request(const struct chef_waiter_build_request* request)
{
    return 0;
}

void chef_waiterd_build_invocation(
    struct gracht_message*                  message,
    const struct chef_waiter_build_request* request)
{
    struct waiterd_cook*    cook;
    struct waiterd_request* wreq;
    VLOG_DEBUG("api", "waiter::build(arch=%u)\n", request->arch);

    if (__verify_build_request(request)) {
        VLOG_ERROR("api", "build request could not be verified\n");
        chef_waiterd_build_response(message, CHEF_QUEUE_STATUS_INTERNAL_ERROR, "0");
        return;
    }

    cook = waiterd_server_cook_find(waiterd_architecture(request->arch));
    if (cook == NULL) {
        VLOG_WARNING("api", "no cook for requested architecture\n");
        chef_waiterd_build_response(message, CHEF_QUEUE_STATUS_NO_COOK_FOR_ARCHITECTURE, "0");
        return;
    }

    wreq = waiterd_server_request_new(cook, message);
    if (wreq == NULL) {
        VLOG_WARNING("api", "failed to allocate memory for build request!!\n");
        chef_waiterd_build_response(message, CHEF_QUEUE_STATUS_INTERNAL_ERROR, "0");
        return;
    }

    // redirect request
    chef_waiterd_cook_event_build_request_single(message->server, cook->client, &wreq->guid[0], request);
}

void chef_waiterd_status_invocation(struct gracht_message* message, const char* id)
{
    struct waiterd_request* wreq;
    VLOG_DEBUG("api", "waiter::status(id=%u)\n", id);

    wreq = waiterd_server_request_find(id);
    if (wreq == NULL) {
        VLOG_WARNING("api", "invalid request id %s\n", id);
        chef_waiterd_status_response(message, CHEF_BUILD_STATUS_UNKNOWN);
        return;
    }

    chef_waiterd_status_response(message, &(struct chef_waiter_status_response) {
        .arch = chef_build_architecture(wreq->architecture),
        .status = wreq->status
    });
}

void chef_waiterd_artifact_invocation(struct gracht_message* message, const char* id, const enum chef_artifact_type type)
{
    struct waiterd_request* wreq;
    VLOG_DEBUG("api", "waiter::artifact(id=%u, type=%u)\n", id, type);

    wreq = waiterd_server_request_find(id);
    if (wreq == NULL) {
        VLOG_WARNING("api", "invalid request id %s\n", id);
        chef_waiterd_artifact_response(message, "");
        return;
    }

    switch (type)  {
        case CHEF_ARTIFACT_TYPE_LOG:
            chef_waiterd_artifact_response(message, wreq->artifacts.log);
            break;
        case CHEF_ARTIFACT_TYPE_PACKAGE:
            chef_waiterd_artifact_response(message, wreq->artifacts.package);
            break;
    }
}
