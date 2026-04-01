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
#include <string.h>
#include <vlog.h>

#include "api.h"
#include "chef_served_service_server.h"

void served_api_convert_app_to_info(struct state_application* application, struct chef_served_package* info)
{
    char versionBuffer[32];

    // Use the first revision's version information if available
    if (application->revisions_count > 0 && application->revisions[0].version != NULL) {
        sprintf(&versionBuffer[0], "%i.%i.%i.%i",
                application->revisions[0].version->major,
                application->revisions[0].version->minor,
                application->revisions[0].version->patch,
                application->revisions[0].version->revision
        );
    } else {
        sprintf(&versionBuffer[0], "0.0.0.0");
    }

    info->name = (char*)application->name;
    info->version = strdup(&versionBuffer[0]);
}

void served_api_cleanup_info(struct chef_served_package* info)
{
    free(info->version);
}

void chef_served_info_invocation(struct gracht_message* message, const char* packageName)
{
    struct state_application*   applications;
    int                         count;
    struct chef_served_package* info;
    struct chef_served_package  zero = { 0 };
    int                         status;
    VLOG_DEBUG("api", "chef_served_info_invocation(package=%s)\n", packageName);

    served_state_lock();
    status = served_state_get_applications(&applications, &count);
    if (status != 0 || count == 0) {
        served_state_unlock();
        VLOG_WARNING("api", "failed to retrieve applications from state\n");
        chef_served_info_response(message, &zero);
        return;
    }

    info = (struct chef_served_package*)malloc(sizeof(struct chef_served_package));
    if (info == NULL) {
        served_state_unlock();
        VLOG_WARNING("api", "failed to allocate memory!\n");
        chef_served_info_response(message, &zero);
        return;
    }

    for (int i = 0; i < count; i++) {
        if (strcmp(applications[i].name, packageName) == 0) {
            served_api_convert_app_to_info(&applications[i], info);
            served_state_unlock();

            // this can be done without the lock
            chef_served_info_response(message, info);
            served_api_cleanup_info(info);
            free(info);
            return;
        }
    }

    served_state_unlock();
    chef_served_info_response(message, &zero);
    free(info);
}
