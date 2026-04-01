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

#include <runner.h>
#include "api.h"
#include "chef_served_service_server.h"

static unsigned int __create_transaction(
    enum served_transaction_type type,
    const char*                  packageName,
    const char*                  channel,
    int                          revision)
{
    unsigned int transactionId;
    char         nameBuffer[256];
    char         descriptionBuffer[512];
    const char*  label = (type == SERVED_TRANSACTION_TYPE_UPDATE) ? "Update" : "Install";

    snprintf(nameBuffer, sizeof(nameBuffer), "%s via API (%s)", label, packageName);
    snprintf(descriptionBuffer, sizeof(descriptionBuffer),
             "%s of package '%s' requested via served API", label, packageName);

    // served_transaction_create persists to DB *and* adds the runtime
    // object to the runner's active queue so it actually gets executed.
    transactionId = served_transaction_create(
        &(struct served_transaction_options){
            .name = &nameBuffer[0],
            .description = &descriptionBuffer[0],
            .type = type,
        }, &(struct state_transaction){
            .name = (char*)packageName,
            .channel = (char*)channel,
            .revision = revision,
        }
    );
    return transactionId;
}

unsigned int served_api_create_install_transaction(const char* packageName, const char* channel, int revision)
{
    struct state_application*    application;
    enum served_transaction_type type;
    char**                       nameParts = NULL;

    // package-name must be in the correct format
    nameParts = utils_split_package_name(packageName);
    if (nameParts == NULL) {
        VLOG_WARNING("api", "invalid package name format: %s\n", packageName);
        return 0;
    }
    strsplit_free(nameParts);

    // Check whether this is an install or an update while holding the
    // state lock, then release before calling served_transaction_create
    // (which takes the lock internally).
    served_state_lock();
    application = served_state_application(packageName);
    type = (application != NULL)
         ? SERVED_TRANSACTION_TYPE_UPDATE
         : SERVED_TRANSACTION_TYPE_INSTALL;
    served_state_unlock();

    return __create_transaction(type, packageName, channel, revision);
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
