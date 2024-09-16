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

void chef_waiterd_cook_ready_invocation(struct gracht_message* message, const struct chef_cook_ready_event* evt)
{
    waiterd_server_cook_ready(message->client, evt->arch);
}

void chef_waiterd_cook_update_invocation(struct gracht_message* message, const struct chef_cook_update_event* evt)
{
    // stats stuff
    // TODO
}

void chef_waiterd_cook_status_invocation(struct gracht_message* message, const struct chef_cook_build_event* evt)
{
    struct waiterd_request*   wreq;
    enum waiterd_build_status status;

    wreq = waiterd_server_request_find(evt->id);
    if (wreq == NULL) {
        // ehh shouldn't happen
        return;
    }

    // Store previous status temporarily
    status = wreq->status;
    wreq->status = waiterd_build_status(evt->status);

    // If it's the first update, then we heard back from the cook
    // whether it started the request. Notify the client of the new status.
    if (status == WAITERD_BUILD_STATUS_UNKNOWN) {
        chef_waiterd_build_response(wreq->source, evt->status, &wreq->guid[0]);
    }
}

void chef_waiterd_cook_artifact_invocation(struct gracht_message* message, const struct chef_cook_artifact_event* evt)
{
    struct waiterd_request* wreq;

    wreq = waiterd_server_request_find(evt->id);
    if (wreq == NULL) {
        // ehh shouldn't happen
        return;
    }

    switch (evt->type) {
        case CHEF_ARTIFACT_TYPE_LOG:
            wreq->artifacts.log = evt->uri;
            break;
        case CHEF_ARTIFACT_TYPE_PACKAGE:
            wreq->artifacts.package = evt->uri;
            break;
    }
}
