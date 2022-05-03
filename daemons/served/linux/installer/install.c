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
#include <chef/package.h>
#include <errno.h>
#include <installer.h>
#include <gracht/server.h>
#include <state.h>

// server protocol
#include "chef_served_service_server.h"

static int __parse_package(const char* path, struct served_application** applicationOut)
{
    struct served_application* application;
    struct chef_package*       package;
    struct chef_version*       version;
    int                        status;

    status = chef_package_load(path, &package, &version);
    if (status) {
        return status;
    }

    // In theory this should also verify the signed signature...
    application = served_application_new();
    if (application == NULL) {
        chef_package_free(package);
        chef_version_free(version);
        return -1;
    }


    *applicationOut = application;
    chef_package_free(package);
    chef_version_free(version);
    return 0;
}

static int __install(const char* path)
{

    return -1;
}

static int __is_in_state(struct served_application* application)
{
    struct served_application** applications;
    int                         count;
    int                         status;

    status = served_state_get_applications(&applications, &count);
    if (status) {
        return status;
    }

    for (int i = 0; i < count; i++) {
        if (strcmp(applications[i]->name, application->name) == 0) {
            return 0;
        }
    }

    errno = ENOENT;
    return -1;
}

static int __load_application(struct served_application* application)
{
    int status;

    status = served_application_ensure_paths(application);
    if (status != 0) {
        // log
        return status;
    }

    status = served_application_mount(application);
    if (status != 0) {
        // log
        return status;
    }

    status = served_application_start_daemons(application);
    if (status != 0) {
        // log
        return status;
    }
    return 0;
}

static void __update(void)
{

}

void served_installer_install(const char* path)
{
    struct served_application* application;
    struct chef_package_info   result = { 0 };
    int                        status;

    // Parse and validate the pack provided
    status = __parse_package(path, &application);
    if (status) {
        chef_served_event_package_installed_all(NULL, CHEF_INSTALL_STATUS_FAILED_INSTALL, &result);
        return;
    }

    // Copy the pack into the storage
    status = __install(path);
    if (status) {
        chef_served_event_package_installed_all(NULL, CHEF_INSTALL_STATUS_FAILED_INSTALL, &result);
        return;
    }

    // Is this package already installed in a different version?
    if (__is_in_state(application)) {
        // OK lets update instead
        __update();
        return;
    }

    // Add it to state
    status = served_state_add_application(application);
    if (status) {
        chef_served_event_package_installed_all(NULL, CHEF_INSTALL_STATUS_FAILED_INSTALL, &result);
        return;
    }

    // Perform initialization sequence as in startup.c
    status = __load_application(application);
    if (status) {
        chef_served_event_package_installed_all(NULL, CHEF_INSTALL_STATUS_FAILED_INSTALL, &result);
        return;
    }

    // TODO: run install hook
    chef_served_event_package_installed_all(NULL, CHEF_INSTALL_STATUS_SUCCESS, &result);
}
