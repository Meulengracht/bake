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
#include <runner.h>
#include <state.h>
#include <stdio.h>
#include <stdlib.h>
#include <vlog.h>

#include <transaction/transaction.h>
#include <transaction/logging.h>
#include <state.h>

// server protocol
#include "chef_served_service_server.h"

static void __convert_app_to_info(struct state_application* application, struct chef_served_package* info)
{
    char versionBuffer[32];

    // Use the first revision's version information if available
    if (application->revisions_count > 0 && application->revisions[0].version != NULL) {
        sprintf(&versionBuffer[0], "%i.%i.%i.%i",
                application->revisions[0].version->major,
                application->revisions[0].version->minor,
                application->revisions[0].version->patch,
                application->revisions[0].version->revision
        );
    } else {
        sprintf(&versionBuffer[0], "0.0.0.0");
    }

    info->name = (char*)application->name;
    info->version = strdup(&versionBuffer[0]);
}

static void __cleanup_info(struct chef_served_package* info)
{
    free(info->version);
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

    served_state_transaction_state_new(
        transactionId, 
        &(struct state_transaction){
            .name = options->package,
            .channel = options->channel,
            .revision = options->revision,
        }
    );
    served_state_unlock();
    chef_served_install_response(message, transactionId);
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

    served_state_transaction_state_new(
        transactionId,
        &(struct state_transaction){
            .name = packageName,
        }
    );
    served_state_unlock();
    chef_served_remove_response(message, transactionId);
}

void chef_served_update_invocation(struct gracht_message* message, const struct chef_served_update_options* options)
{
    unsigned int transactionId;
    char         nameBuffer[256];
    char         descriptionBuffer[512];
    
    // Update options now contains an array of packages to update
    // For now, we'll handle the first package if any are provided
    if (options->packages_count == 0) {
        VLOG_WARNING("api", "chef_served_update_invocation: no packages specified\n");
        return;
    }
    
    VLOG_DEBUG("api", "chef_served_update_invocation(package=%s)\n", options->packages[0].name);

    snprintf(nameBuffer, sizeof(nameBuffer), "Update via API (%s)", options->packages[0].name);
    snprintf(descriptionBuffer, sizeof(descriptionBuffer), "Update of package '%s' requested via served API", options->packages[0].name);

    served_state_lock();
    transactionId = served_state_transaction_new(&(struct served_transaction_options){
        .name = &nameBuffer[0],
        .description = &descriptionBuffer[0],
        .type = SERVED_TRANSACTION_TYPE_UPDATE,
    });

    served_state_transaction_state_new(
        transactionId,
        &(struct state_transaction){
            .name = options->packages[0].name,
            .channel = NULL,  // Update options don't specify channel
            .revision = 0,    // Update options don't specify revision
        }
    );
    served_state_unlock();
    chef_served_update_response(message, transactionId);
}

void chef_served_switch_invocation(struct gracht_message* message, const struct chef_served_switch_options* options)
{
    // TODO: Figure out what this means, but ignore it for now
}

void chef_served_info_invocation(struct gracht_message* message, const char* packageName)
{
    struct state_application*   applications;
    int                         count;
    struct chef_served_package* info;
    struct chef_served_package  zero = { 0 };
    int                         status;
    VLOG_DEBUG("api", "chef_served_info_invocation(package=%s)\n", packageName);

    served_state_lock();
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
        if (strcmp(applications[i].name, packageName) == 0) {
            __convert_app_to_info(&applications[i], info);
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
    struct state_application* applications;
    int                       count = 0;
    int                       status;
    VLOG_DEBUG("api", "chef_served_listcount_invocation()\n");

    served_state_lock();
    served_state_get_applications(&applications, &count);
    served_state_unlock();
    chef_served_listcount_response(message, (unsigned int)count);
}

void chef_served_list_invocation(struct gracht_message* message)
{
    struct state_application* applications;
    struct chef_served_package* infos;
    int                         count;
    int                         status;
    VLOG_DEBUG("api", "chef_served_list_invocation()\n");

    served_state_lock();
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
        __convert_app_to_info(&applications[i], &infos[i]);
    }

    // we can unlock from here as we do not need to access the state anymore
    served_state_unlock();

    chef_served_list_response(message, infos, count);
    for (int i = 0; i < count; i++) {
        __cleanup_info(&infos[i]);
    }
    free(infos);
}

void chef_served_logs_invocation(struct gracht_message* message, unsigned int transaction_id)
{
    struct state_transaction_log*      logs = NULL;
    struct chef_transaction_log_entry* entries = NULL;
    int                                count = 0;
    int                                i;
    
    VLOG_DEBUG("api", "chef_served_logs_invocation(transaction_id=%u)\n", transaction_id);
    
    served_state_lock();
    if (served_state_transaction_logs(transaction_id, &logs, &count) != 0 || count == 0) {
        served_state_unlock();
        chef_served_logs_response(message, NULL, 0);
        return;
    }
    
    // Allocate array for response
    entries = calloc(count, sizeof(struct chef_transaction_log_entry));
    if (entries == NULL) {
        served_state_unlock();
        VLOG_ERROR("api", "failed to allocate memory for log entries\n");
        chef_served_logs_response(message, NULL, 0);
        return;
    }
    
    // Copy log entries from state
    for (i = 0; i < count; i++) {
        struct state_transaction_log* log = &logs[i];
        enum chef_transaction_log_level level;
        enum chef_transaction_state state;
        
        // Map log level
        switch (log->level) {
        case SERVED_TRANSACTION_LOG_INFO:
            level = CHEF_TRANSACTION_LOG_LEVEL_INFO;
            break;
        case SERVED_TRANSACTION_LOG_WARNING:
            level = CHEF_TRANSACTION_LOG_LEVEL_WARNING;
            break;
        case SERVED_TRANSACTION_LOG_ERROR:
            level = CHEF_TRANSACTION_LOG_LEVEL_ERROR;
            break;
        default:
            level = CHEF_TRANSACTION_LOG_LEVEL_INFO;
            break;
        }
        
        // Map state using the public mapping function
        state = served_transaction_map_state(log->state);
        
        entries[i].level = level;
        entries[i].timestamp = (unsigned long long)log->timestamp;
        entries[i].state = state;
        entries[i].message = platform_strdup(log->message);
    }
    
    served_state_unlock();
    
    chef_served_logs_response(message, entries, count);
    
    // Cleanup
    for (i = 0; i < count; i++) {
        free(entries[i].message);
    }
    free(entries);
}
