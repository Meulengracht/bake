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

#include <application.h>
#include <chef/package.h>
#include <chef/platform.h>
#include <errno.h>
#include <installer.h>
#include <state.h>
#include <stdio.h>
#include <stdlib.h>
#include <utils.h>
#include <vlog.h>

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

static int __parse_package(const char* publisher, const char* path, struct served_application** applicationOut)
{
    struct served_application* application;
    struct chef_package*       package;
    struct chef_version*       version;
    struct chef_command*       commands;
    int                        count;
    int                        status;

    status = chef_package_load(path,
                               &package,
                               &version,
                               &commands,
                               &count
    );
    if (status) {
        return status;
    }

    // In theory this should also verify the signed signature...
    application = served_application_new();
    if (application == NULL) {
        status = -1;
        goto cleanup;
    }

    application->name = __build_application_name(publisher, package->package);
    if (application->name == NULL) {
        status = -1;
        goto cleanup;
    }

    application->publisher = strdup(publisher);
    application->package   = strdup(package->package);
    if (application->publisher == NULL || application->package == NULL) {
        status = -1;
        goto cleanup;
    }

    application->major    = version->major;
    application->minor    = version->minor;
    application->patch    = version->patch;
    application->revision = version->revision;

    application->commands_count = count;
    if (count) {
        application->commands = calloc(count, sizeof(struct served_command));
        if (application->commands == NULL) {
            status = -1;
            goto cleanup;
        }

        for (int i = 0; i < count; i++) {
            application->commands[i].type      = (int)commands[i].type;
            application->commands[i].name      = strdup(commands[i].name);
            application->commands[i].path      = strdup(commands[i].path);
            application->commands[i].arguments = commands[i].arguments ? strdup(commands[i].arguments) : NULL;
        }
    }

    *applicationOut = application;

cleanup:
    chef_package_free(package);
    chef_version_free(version);
    chef_commands_free(commands, count);
    if (status) {
        served_application_delete(application);
    }
    return status;
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
    struct chef_served_package  result = { 0 };
    int                         count;
    int                         status;
    VLOG_TRACE("update", "__update(path=%s, name=%s)\n", path, name);

    status = served_state_get_applications(&applications, &count);
    if (status) {
        VLOG_ERROR("update", "Failed to get applications from state\n");
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
        VLOG_ERROR("update", "Failed to find application %s in state\n", name);
        chef_served_event_package_updated_all(served_gracht_server(), CHEF_UPDATE_STATUS_FAILED_INSTALL, &result);
        return;
    }

    // TODO check if it is running, then we need to turn it off and mark it as updating
    // so we don't start it again

    // TODO run the pre-update hook

    status = served_application_unload(application);
    if (status != 0) {
        VLOG_ERROR("update", "Failed to unload application %s\n", name);
        chef_served_event_package_updated_all(served_gracht_server(), CHEF_UPDATE_STATUS_FAILED_INSTALL, &result);
        return;
    }

    status = __install(path, application);
    if (status) {
        VLOG_ERROR("update", "Failed to update application %s\n", name);
        chef_served_event_package_updated_all(served_gracht_server(), CHEF_UPDATE_STATUS_FAILED_INSTALL, &result);
        return;
    }

    status = served_application_load(application);
    if (status) {
        VLOG_ERROR("update", "Failed to load application %s\n", name);
        chef_served_event_package_updated_all(served_gracht_server(), CHEF_UPDATE_STATUS_FAILED_INSTALL, &result);
        return;
    }

    // TODO run the post-update hook


    __convert_app_to_info(application, &result);
    chef_served_event_package_updated_all(served_gracht_server(), CHEF_UPDATE_STATUS_SUCCESS, &result);
    __cleanup_info(&result);
}

void served_installer_install(const char* publisher, const char* path)
{
    struct served_application* application;
    struct chef_served_package result = { 0 };
    int                        status;
    VLOG_TRACE("install", "served_installer_install(publisher=%s, path=%s)\n", publisher, path);

    status = __parse_package(publisher, path, &application);
    if (status) {
        VLOG_ERROR("install", "failed to parse %s\n", path);
        chef_served_event_package_installed_all(served_gracht_server(), CHEF_INSTALL_STATUS_FAILED_INSTALL, &result);
        return;
    }

    status = served_state_lock();
    if (status) {
        VLOG_ERROR("install", "failed to lock state\n");
        return;
    }

    // If the application is already installed, then we perform an update sequence instead of
    // an installation sequence.
    if (__is_in_state(application) == 0) {
        VLOG_TRACE("install", "%s was already installed, switching to update mode\n", application->name);
        __update(path, application->name);
        served_state_unlock();
        return;
    }

    status = __install(path, application);
    if (status) {
        served_state_unlock();
        VLOG_ERROR("install", "installation failed\n");
        chef_served_event_package_installed_all(served_gracht_server(), CHEF_INSTALL_STATUS_FAILED_INSTALL, &result);
        return;
    }

    status = served_state_add_application(application);
    if (status) {
        served_state_unlock();
        VLOG_ERROR("install", "failed to add application to state\n");
        chef_served_event_package_installed_all(served_gracht_server(), CHEF_INSTALL_STATUS_FAILED_INSTALL, &result);
        return;
    }

    status = served_application_load(application);
    if (status) {
        served_state_unlock();
        VLOG_ERROR("install", "failed to load application\n");
        chef_served_event_package_installed_all(served_gracht_server(), CHEF_INSTALL_STATUS_FAILED_INSTALL, &result);
        return;
    }

    served_state_unlock();

    // TODO: run install hook

    __convert_app_to_info(application, &result);
    chef_served_event_package_installed_all(served_gracht_server(), CHEF_INSTALL_STATUS_SUCCESS, &result);
    __cleanup_info(&result);
}
