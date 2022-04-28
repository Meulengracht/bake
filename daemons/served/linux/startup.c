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

int served_startup(void)
{
    struct served_application* applications;
    int                        applicationCount;
    int                        status;

    // Load registry of installed applications
    status = served_state_load();
    if (status != 0) {
        return status;
    }

    status = served_state_get_applications(&applications, &applicationCount);
    if (status != 0) {
        // log
        return status;
    }

    for (int i = 0; i < applicationCount; i++) {
        status = served_application_mount(&applications[i]);
        if (status != 0) {
            // log
            continue;
        }

        status = served_application_start_daemons(&applications[i]);
        if (status != 0) {
            // log
        }
    }

    return 0;
}
