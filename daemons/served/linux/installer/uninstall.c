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
#include <chef/platform.h>
#include <installer.h>
#include <state.h>
#include <string.h>
#include <utils.h>

// server protocol
#include "chef_served_service_server.h"

static struct served_application* __get_application(const char* name)
{
    struct served_application** applications;
    int                         count;
    int                         status;

    status = served_state_get_applications(&applications, &count);
    if (status) {
        return NULL;
    }

    for (int i = 0; i < count; i++) {
        if (strcmp(applications[i]->name, name) == 0) {
            return applications[i];
        }
    }
    return NULL;
}

static int __remove_package(struct served_application* application)
{
    const char* storagePath = served_application_get_pack_path(application);
    int         status;

    if (storagePath == NULL) {
        return -1;
    }

    status = platform_unlink(storagePath);
    free((void*)storagePath);
    return status;
}

void served_installer_uninstall(const char* package)
{
    struct served_application* application;
    int                        status;

    status = served_state_lock();
    if (status) {
        return;
    }

    application = __get_application(package);
    if (application == NULL) {
        served_state_unlock();
        return;
    }

    // TODO run uninstall hook

    status = served_application_unload(application);
    if (status) {
        served_state_unlock();
        return;
    }

    status = served_state_remove_application(application);
    if (status) {
        served_state_unlock();
        return;
    }
    served_state_unlock();

    status = __remove_package(application);
    if (status) {
        return;
    }
}
