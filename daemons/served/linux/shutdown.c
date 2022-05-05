/**
 * Copyright 2022, Philip Meulengracht
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

#include <application.h>
#include <startup.h>
#include <state.h>
#include <vlog.h>

void served_shutdown(void)
{
    struct served_application** applications;
    int                         applicationCount;
    int                         status;
    VLOG_TRACE("shutdown", "served_shutdown()\n");

    status = served_state_get_applications(&applications, &applicationCount);
    if (status != 0) {
        VLOG_ERROR("shutdown", "failed to load applications from state, this could be serious\n");
        goto save_state;
    }

    for (int i = 0; i < applicationCount; i++) {
        status = served_application_stop_daemons(applications[i]);
        if (status != 0) {
            VLOG_WARNING("shutdown", "failed to stop daemons for application %s\n", applications[i]->name);
        }

        status = served_application_unmount(applications[i]);
        if (status != 0) {
            VLOG_WARNING("shutdown", "failed to unmount application %s\n", applications[i]->name);
        }
    }

save_state:
    status = served_state_save();
    if (status) {
        VLOG_ERROR("shutdown", "failed to save state!!!\n");
    }
}
