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

#include <gracht/server.h>
#include <state.h>
#include <stdio.h>
#include <stdlib.h>
#include <vlog.h>

#include "api.h"
#include "chef_served_service_server.h"

void chef_served_listcount_invocation(struct gracht_message* message)
{
    struct state_application* applications;
    int                       count = 0;
    VLOG_DEBUG("api", "chef_served_listcount_invocation()\n");

    served_state_lock();
    served_state_get_applications(&applications, &count);
    served_state_unlock();
    chef_served_listcount_response(message, (unsigned int)count);
}

void chef_served_list_invocation(struct gracht_message* message)
{
    struct state_application* applications;
    struct chef_served_package* infos;
    int                         count;
    int                         status;
    VLOG_DEBUG("api", "chef_served_list_invocation()\n");

    served_state_lock();
    status = served_state_get_applications(&applications, &count);
    if (status != 0 || count == 0) {
        served_state_unlock();
        VLOG_WARNING("api", "failed to retrieve applications from state\n");
        chef_served_list_response(message, NULL, 0);
        return;
    }

    infos = (struct chef_served_package*)malloc(sizeof(struct chef_served_package) * count);
    if (infos == NULL) {
        served_state_unlock();
        VLOG_WARNING("api", "failed to allocate memory!\n");
        chef_served_list_response(message, NULL, 0);
        return;
    }

    for (int i = 0; i < count; i++) {
        served_api_convert_app_to_info(&applications[i], &infos[i]);
    }

    // we can unlock from here as we do not need to access the state anymore
    served_state_unlock();

    chef_served_list_response(message, infos, count);
    for (int i = 0; i < count; i++) {
        served_api_cleanup_info(&infos[i]);
    }
    free(infos);
}
