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
#include <utils.h>
#include <vlog.h>

#include <transaction/transaction.h>

#include "api.h"
#include "chef_served_service_server.h"

static unsigned int __schedule_update(const char* packageName, const char* channel, int revision)
{
    unsigned int transactionId;
    char         nameBuffer[256];
    char         descriptionBuffer[512];

    snprintf(nameBuffer, sizeof(nameBuffer), "Update via API (%s)", packageName);
    snprintf(descriptionBuffer, sizeof(descriptionBuffer), "Update of package '%s' requested via served API", packageName);

    transactionId = served_state_transaction_new(&(struct served_transaction_options){
        .name = &nameBuffer[0],
        .description = &descriptionBuffer[0],
        .type = SERVED_TRANSACTION_TYPE_UPDATE,
    });

    served_state_transaction_state_new(
        transactionId,
        &(struct state_transaction){
            .name = (char*)packageName,
            .channel = (char*)channel,
            .revision = revision,
        }
    );
    return transactionId;
}

static unsigned int __schedule_install(const char* packageName, const char* channel, int revision)
{
    unsigned int transactionId;
    char         nameBuffer[256];
    char         descriptionBuffer[512];

    snprintf(nameBuffer, sizeof(nameBuffer), "Install via API (%s)", packageName);
    snprintf(descriptionBuffer, sizeof(descriptionBuffer), "Installation of package '%s' requested via served API", packageName);

    transactionId = served_state_transaction_new(&(struct served_transaction_options){
        .name = &nameBuffer[0],
        .description = &descriptionBuffer[0],
        .type = SERVED_TRANSACTION_TYPE_INSTALL,
    });

    served_state_transaction_state_new(
        transactionId,
        &(struct state_transaction){
            .name = (char*)packageName,
            .channel = (char*)channel,
            .revision = revision,
        }
    );
    return transactionId;
}

unsigned int served_api_create_install_transaction(const char* packageName, const char* channel, int revision)
{
    unsigned int              transactionId;
    struct state_application* application;
    char**                    nameParts = NULL;

    // package-name must be in the correct format
    nameParts = utils_split_package_name(packageName);
    if (nameParts == NULL) {
        VLOG_WARNING("api", "invalid package name format: %s\n", packageName);
        return 0;
    }
    strsplit_free(nameParts);

    served_state_lock();

    // If the package is already installed, we should schedule an update transaction
    application = served_state_application(packageName);
    if (application != NULL) {
        transactionId = __schedule_update(packageName, channel, revision);
    } else {
        transactionId = __schedule_install(packageName, channel, revision);
    }

    served_state_unlock();
    return transactionId;
}

void chef_served_install_invocation(struct gracht_message* message, const struct chef_served_install_options* options)
{
    char* packageName;

    VLOG_DEBUG("api", "chef_served_install_invocation(package=%s)\n", options->package);

    if (options->package == NULL || options->package[0] == '\0') {
        chef_served_install_response(message, 0);
        return;
    }

    packageName = platform_strdup(options->package);
    if (packageName == NULL) {
        chef_served_install_response(message, 0);
        return;
    }

    chef_served_install_response(
        message,
        served_api_create_install_transaction(packageName, options->channel, options->revision)
    );
    free(packageName);
}
