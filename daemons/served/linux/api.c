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
#include <stdlib.h>

// server protocol
#include "chef_served_service_server.h"

static void __convert_app_to_protocol(struct served_application* application, struct chef_package_info* info)
{
    // TODO
    info->name = (char*)application->name;
    info->version = application->revision;
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
    struct chef_package_info*   info;
    struct chef_package_info    zero = { 0 };
    int                         status;

    status = served_state_get_applications(&applications, &count);
    if (status != 0 || count == 0) {
        chef_served_info_response(message, &zero);
        return;
    }

    info = (struct chef_package_info*)malloc(sizeof(struct chef_package_info));
    if (info == NULL) {
        chef_served_info_response(message, &zero);
        return;
    }

    for (int i = 0; i < count; i++) {
        if (strcmp(applications[i]->name, packageName) == 0) {
            __convert_app_to_protocol(applications[i], info);
            chef_served_info_response(message, info);
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
    struct chef_package_info*   infos;
    int                         count;
    int                         status;

    status = served_state_get_applications(&applications, &count);
    if (status != 0 || count == 0) {
        chef_served_list_response(message, NULL, 0);
        return;
    }

    infos = (struct chef_package_info*)malloc(sizeof(struct chef_package_info) * count);
    if (infos == NULL) {
        chef_served_list_response(message, NULL, 0);
        return;
    }

    for (int i = 0; i < count; i++) {
        __convert_app_to_protocol(applications[i], &infos[i]);
    }

    chef_served_list_response(message, infos, count);
    free(infos);
}
