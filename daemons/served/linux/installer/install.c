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
#include <chef/platform.h>
#include <errno.h>
#include <installer.h>
#include <state.h>
#include <stdio.h>
#include <stdlib.h>
#include <utils.h>

// server protocol
#include "chef_served_service_server.h"

static const char* __build_application_name(const char* publisher, const char* package)
{
    char* buffer;

    if (publisher == NULL || package == NULL) {
        return NULL;
    }

    buffer = malloc(strlen(publisher) + strlen(package) + 2);
    if (buffer == NULL) {
        return NULL;
    }

    sprintf(&buffer[0], "%s/%s", publisher, package);
    return buffer;
}

static int __parse_package(const char* path, struct served_application** applicationOut)
{
    struct served_application* application;
    struct chef_package*       package;
    struct chef_version*       version;
    int                        status;

    status = chef_package_load(path, &package, &version, NULL, NULL);
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

    application->name     = __build_application_name(package->publisher, package->package);
    application->major    = version->major;
    application->minor    = version->minor;
    application->patch    = version->patch;
    application->revision = version->revision;

    // TODO load commands

    *applicationOut = application;
    chef_package_free(package);
    chef_version_free(version);
    return 0;
}

static int __install(const char* path, struct served_application* application)
{
    const char* storagePath = served_application_get_pack_path(application);
    int         status;

    if (storagePath == NULL) {
        return -1;
    }

    status = platform_copyfile(path, storagePath);
    free((void*)storagePath);
    return status;
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

static void __convert_app_to_info(struct served_application* application, struct chef_served_package* info)
{
    char versionBuffer[32];

    sprintf(&versionBuffer[0], "%i.%i.%i.%i",
            application->major, application->minor,
            application->patch, application->revision
    );

    info->name = (char*)application->name;
    info->version = strdup(&versionBuffer[0]);
}

static void __cleanup_info(struct chef_served_package* info)
{
    free(info->version);
}

static void __update(const char* path, const char* name)
{
    struct served_application*  application = NULL;
    struct served_application** applications;
    struct chef_served_package    result = { 0 };
    int                         count;
    int                         status;

    status = served_state_get_applications(&applications, &count);
    if (status) {
        chef_served_event_package_updated_all(served_gracht_server(), CHEF_UPDATE_STATUS_FAILED_INSTALL, &result);
        return;
    }

    for (int i = 0; i < count; i++) {
        if (strcmp(applications[i]->name, name) == 0) {
            application = applications[i];
            break;
        }
    }

    if (application == NULL) {
        // THIS SHOULD NEVER HAPPEN!!
        chef_served_event_package_updated_all(served_gracht_server(), CHEF_UPDATE_STATUS_FAILED_INSTALL, &result);
        return;
    }

    // check if it is running, then we need to turn it off and mark it as updating
    // so we don't start it again

    // TODO run the pre-update hook

    status = served_application_unload(application);
    if (status != 0) {
        chef_served_event_package_updated_all(served_gracht_server(), CHEF_UPDATE_STATUS_FAILED_INSTALL, &result);
        return;
    }

    status = __install(path, application);
    if (status) {
        chef_served_event_package_updated_all(served_gracht_server(), CHEF_UPDATE_STATUS_FAILED_INSTALL, &result);
        return;
    }

    status = served_application_load(application);
    if (status) {
        chef_served_event_package_updated_all(served_gracht_server(), CHEF_UPDATE_STATUS_FAILED_INSTALL, &result);
        return;
    }

    // TODO run the post-update hook


    __convert_app_to_info(application, &result);
    chef_served_event_package_updated_all(served_gracht_server(), CHEF_UPDATE_STATUS_SUCCESS, &result);
    __cleanup_info(&result);
}

void served_installer_install(const char* path)
{
    struct served_application* application;
    struct chef_served_package   result = { 0 };
    int                        status;

    status = __parse_package(path, &application);
    if (status) {
        chef_served_event_package_installed_all(served_gracht_server(), CHEF_INSTALL_STATUS_FAILED_INSTALL, &result);
        return;
    }

    status = served_state_lock();
    if (status) {
        return;
    }

    // If the application is already installed, then we perform an update sequence instead of
    // an installation sequence.
    if (__is_in_state(application)) {
        __update(path, application->name);
        served_state_unlock();
        return;
    }

    status = __install(path, application);
    if (status) {
        chef_served_event_package_installed_all(served_gracht_server(), CHEF_INSTALL_STATUS_FAILED_INSTALL, &result);
        return;
    }

    status = served_state_add_application(application);
    if (status) {
        served_state_unlock();
        chef_served_event_package_installed_all(served_gracht_server(), CHEF_INSTALL_STATUS_FAILED_INSTALL, &result);
        return;
    }

    status = served_application_load(application);
    if (status) {
        served_state_unlock();
        chef_served_event_package_installed_all(served_gracht_server(), CHEF_INSTALL_STATUS_FAILED_INSTALL, &result);
        return;
    }

    served_state_unlock();

    // TODO: run install hook

    __convert_app_to_info(application, &result);
    chef_served_event_package_installed_all(served_gracht_server(), CHEF_INSTALL_STATUS_SUCCESS, &result);
    __cleanup_info(&result);
}
