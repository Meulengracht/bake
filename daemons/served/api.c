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

#include <chef/platform.h>
#include <gracht/server.h>
#include <state.h>
#include <stdio.h>
#include <stdlib.h>
#include <vlog.h>

#include <transaction/transaction.h>
#include <state.h>

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

static void __convert_cmd_to_protocol(struct served_command* command, struct chef_served_command* proto)
{
    proto->type      = command->type;
    proto->path      = (char*)command->path;
    proto->arguments = (char*)command->arguments;
    proto->data_path = (char*)command->data;
}

void chef_served_install_invocation(struct gracht_message* message, const struct chef_served_install_options* options)
{
    unsigned int transactionId;
    char         nameBuffer[256];
    char         descriptionBuffer[512];
    VLOG_DEBUG("api", "chef_served_install_invocation(publisher=%s, path=%s)\n", options->package, options->path);

    snprintf(nameBuffer, sizeof(nameBuffer), "Install via API (%s)", options->package);
    snprintf(descriptionBuffer, sizeof(descriptionBuffer), "Installation of package from publisher '%s' requested via served API", options->package);

    served_state_lock();
    transactionId = served_state_transaction_new(&(struct served_transaction_options){
        .name = &nameBuffer[0],
        .description = &descriptionBuffer[0],
        .type = SERVED_TRANSACTION_TYPE_INSTALL,
    });

    served_state_transaction_state_new(transactionId, &(struct state_transaction){
        .name = options->package,
        .channel = options->channel,
        .revision = options->revision,
    });
    served_state_unlock();
}

void chef_served_remove_invocation(struct gracht_message* message, const char* packageName)
{
    unsigned int transactionId;
    char         nameBuffer[256];
    char         descriptionBuffer[512];
    VLOG_DEBUG("api", "chef_served_remove_invocation(package=%s)\n", packageName);

    snprintf(nameBuffer, sizeof(nameBuffer), "Remove via API (%s)", packageName);
    snprintf(descriptionBuffer, sizeof(descriptionBuffer), "Removal of package '%s' requested via served API", packageName);

    served_state_lock();
    transactionId = served_state_transaction_new(&(struct served_transaction_options){
        .name = &nameBuffer[0],
        .description = &descriptionBuffer[0],
        .type = SERVED_TRANSACTION_TYPE_UNINSTALL,
    });

    served_state_transaction_state_new(transactionId, &(struct state_transaction){
        .name = packageName,
    });
    served_state_unlock();
}

void chef_served_info_invocation(struct gracht_message* message, const char* packageName)
{
    struct served_application** applications;
    int                         count;
    struct chef_served_package* info;
    struct chef_served_package  zero = { 0 };
    int                         status;
    VLOG_DEBUG("api", "chef_served_info_invocation(package=%s)\n", packageName);

    status = served_state_lock();
    if (status) {
        VLOG_WARNING("api", "failed to acquire state lock\n");
        chef_served_info_response(message, &zero);
        return;
    }

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
        if (strcmp(applications[i]->name, packageName) == 0) {
            __convert_app_to_info(applications[i], info);
            served_state_unlock();

            // this can be done without the lock
            chef_served_info_response(message, info);
            __cleanup_info(info);
            free(info);
            return;
        }
    }

    served_state_unlock();
    chef_served_info_response(message, &zero);
    free(info);
}

void chef_served_listcount_invocation(struct gracht_message* message)
{
    struct served_application** applications;
    int                         count = 0;
    int                         status;
    VLOG_DEBUG("api", "chef_served_listcount_invocation()\n");

    served_state_lock();
    served_state_get_applications(&applications, &count);
    served_state_unlock();
    chef_served_listcount_response(message, (unsigned int)count);
}

void chef_served_list_invocation(struct gracht_message* message)
{
    struct served_application** applications;
    struct chef_served_package* infos;
    int                         count;
    int                         status;
    VLOG_DEBUG("api", "chef_served_list_invocation()\n");

    status = served_state_lock();
    if (status) {
        VLOG_WARNING("api", "failed to acquire state lock\n");
        chef_served_list_response(message, NULL, 0);
        return;
    }

    status = served_state_get_applications(&applications, &count);
    if (status != 0 || count == 0) {
        served_state_unlock();
        VLOG_WARNING("api", "failed to retrieve applications from state\n");
        chef_served_list_response(message, NULL, 0);
        return;
    }

    infos = (struct chef_served_package*)malloc(sizeof(struct chef_served_package) * count);
    if (infos == NULL) {
        served_state_unlock();
        VLOG_WARNING("api", "failed to allocate memory!\n");
        chef_served_list_response(message, NULL, 0);
        return;
    }

    for (int i = 0; i < count; i++) {
        __convert_app_to_info(applications[i], &infos[i]);
    }

    // we can unlock from here as we do not need to access the state anymore
    served_state_unlock();

    chef_served_list_response(message, infos, count);
    for (int i = 0; i < count; i++) {
        __cleanup_info(&infos[i]);
    }
    free(infos);
}

void chef_served_get_command_invocation(struct gracht_message* message, const char* mountPath)
{
    struct served_application** applications;
    struct chef_served_command  result;
    int                         count;
    int                         status;
    
    VLOG_DEBUG("api", "chef_served_get_command_invocation(mountPath=%s)\n", mountPath);
    chef_served_command_init(&result);

    status = served_state_lock();
    if (status) {
        VLOG_WARNING("api", "failed to acquire state lock\n");
        chef_served_get_command_response(message, &result);
        return;
    }

    status = served_state_get_applications(&applications, &count);
    if (status) {
        served_state_unlock();
        VLOG_WARNING("api", "failed to retrieve applications from state\n");
        chef_served_get_command_response(message, &result);
        return;
    }

    for (int i = 0; i < count; i++) {
        for (int j = 0; j < applications[i]->commands_count; j++) {
            if (strendswith(applications[i]->commands[j].symlink, mountPath) == 0) {
                __convert_cmd_to_protocol(&applications[i]->commands[j], &result);
                served_state_unlock();
                chef_served_get_command_response(message, &result);
                return;
            }
        }
    }
    served_state_unlock();
    chef_served_get_command_response(message, &result);
}
