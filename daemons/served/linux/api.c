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
#include <gracht/server.h>
#include <installer.h>
#include <state.h>
#include <stdio.h>
#include <stdlib.h>

// server protocol
#include "chef_served_service_server.h"

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

void chef_served_install_invocation(struct gracht_message* message, const char* path)
{
    served_installer_install(path);
}

void chef_served_remove_invocation(struct gracht_message* message, const char* packageName)
{
    served_installer_uninstall(packageName);
}

void chef_served_info_invocation(struct gracht_message* message, const char* packageName)
{
    struct served_application** applications;
    int                         count;
    struct chef_served_package*   info;
    struct chef_served_package    zero = { 0 };
    int                         status;

    status = served_state_get_applications(&applications, &count);
    if (status != 0 || count == 0) {
        chef_served_info_response(message, &zero);
        return;
    }

    info = (struct chef_served_package*)malloc(sizeof(struct chef_served_package));
    if (info == NULL) {
        chef_served_info_response(message, &zero);
        return;
    }

    for (int i = 0; i < count; i++) {
        if (strcmp(applications[i]->name, packageName) == 0) {
            __convert_app_to_info(applications[i], info);
            chef_served_info_response(message, info);
            __cleanup_info(info);
            free(info);
            return;
        }
    }

    chef_served_info_response(message, &zero);
    free(info);
}

void chef_served_listcount_invocation(struct gracht_message* message)
{
    struct served_application** applications;
    int                         count;
    int                         status;

    status = served_state_get_applications(&applications, &count);
    if (status != 0) {
        chef_served_listcount_response(message, 0);
        return;
    }
    chef_served_listcount_response(message, (unsigned int)count);
}

void chef_served_list_invocation(struct gracht_message* message)
{
    struct served_application** applications;
    struct chef_served_package* infos;
    int                         count;
    int                         status;

    status = served_state_get_applications(&applications, &count);
    if (status != 0 || count == 0) {
        chef_served_list_response(message, NULL, 0);
        return;
    }

    infos = (struct chef_served_package*)malloc(sizeof(struct chef_served_package) * count);
    if (infos == NULL) {
        chef_served_list_response(message, NULL, 0);
        return;
    }

    for (int i = 0; i < count; i++) {
        __convert_app_to_info(applications[i], &infos[i]);
    }

    chef_served_list_response(message, infos, count);

    for (int i = 0; i < count; i++) {
        __cleanup_info(&infos[i]);
    }
    free(infos);
}

void chef_served_get_command_invocation(struct gracht_message* message, const char* mountPath)
{
    struct served_application** applications;
    int                         count;
    int                         status;

    status = served_state_lock();
    if (status) {

    }

    status = served_state_get_applications(&applications, &count);
    if (status) {
        served_state_unlock();
        return;
    }

    for (int i = 0; i < count; i++) {
        for (int j = 0; j < applications[i]->commands_count; j++) {
            if (strcmp(applications[i]->commands[j].mount, mountPath) == 0) {
                // TODO
                chef_served_get_command_response(message, NULL);
                served_state_unlock();
                return;
            }
        }
    }
    served_state_unlock();
    chef_served_get_command_response(message, NULL);
}
