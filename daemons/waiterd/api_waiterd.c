/**
 * Copyright 2024, Philip Meulengracht
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

#include "private.h"
#include "chef_waiterd_service_server.h"

void chef_waiterd_build_invocation(
    struct gracht_message*                  message,
    const struct chef_waiter_build_request* request)
{
    // verify request

    // find suitable cook

    // proxy request

    // return status and id of request
}

void chef_waiterd_status_invocation(struct gracht_message* message, const char* id)
{

}
