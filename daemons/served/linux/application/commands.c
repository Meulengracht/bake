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

#include <errno.h>
#include <application.h>
#include <chef/platform.h>
#include <chef/containerv.h>
#include <stdlib.h>

static struct containerv_container* __create_container(struct served_application* application)
{
    struct containerv_container* container;
    int                          status;
    char*                        rootFs;

    rootFs = served_application_get_mount_path(application);
    status = containerv_create(
        rootFs,
        0,
        NULL,
        0,
        &container
    );
    free(rootFs);
    if (status) {
        return NULL;
    }
    return container;
}

int served_application_start_daemons(struct served_application* application)
{
    if (application == NULL) {
        errno = EINVAL;
        return -1;
    }

    application->container = __create_container(application);
    if (application->container == NULL) {
        return -1;
    }

    for (int i = 0; i < application->commands_count; i++) {

    }
    return 0;
}

int served_application_stop_daemons(struct served_application* application)
{
    if (application == NULL) {
        errno = EINVAL;
        return -1;
    }

    for (int i = 0; i < application->commands_count; i++) {

    }
    return 0;
}
