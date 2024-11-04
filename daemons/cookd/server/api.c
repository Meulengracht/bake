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

#include "chef_waiterd_cook_service_client.h"
#include <server.h>
#include <vlog.h>

void chef_waiterd_cook_event_update_request_invocation(gracht_client_t* client, const struct chef_cook_update_request* request)
{
    struct gracht_message_context msg;
    struct cookd_status           info;
    int                           status;
    
    cookd_server_status(&info);

    status = chef_waiterd_cook_update(client, &msg, &(struct chef_cook_update_event) {
        .queue_size = info.queue_size
    });
    if (status) {

    }
}

void chef_waiterd_cook_event_build_request_invocation(gracht_client_t* client, const struct chef_waiter_build_request* request)
{
    int status;

    status = cookd_server_build(NULL, &(struct cookd_build_options) {
        .architecture = request->arch,
        .platform = request->platform,
        .url = request->url,
        .recipe_path = request->recipe
    });
    if (status) {
        
    }
}
