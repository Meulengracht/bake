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

#include <transaction/states/dependencies.h>
#include <transaction/transaction.h>
#include <state.h>
#include <utils.h>

#include <chef/package.h>
#include <chef/platform.h>
#include <errno.h>
#include <string.h>

static sm_event_t __ensure_base(struct served_transaction* transaction, const char* name, const char* base)
{
    struct state_application* application;
    struct state_application* base;
    unsigned int              transactionId;
    char                      nameBuffer[256];
    char                      descriptionBuffer[512];
    
    served_state_lock();
    application = served_state_application(name);
    if (application == NULL) {
        TXLOG_ERROR(
            transaction,
            "Package '%s' not found in state while resolving base dependency",
            name
        );
        served_state_unlock();
        return SERVED_TX_EVENT_FAILED;
    }

    base = served_state_application(base);
    if (base != NULL) {
        // base already installed
        TXLOG_INFO(transaction, "Base for %s already installed", name);
        served_state_unlock();
        return SERVED_TX_EVENT_OK;
    }

    // schedule installation
    snprintf(nameBuffer, sizeof(nameBuffer), "Install dependency (%s)", base);
    snprintf(descriptionBuffer, sizeof(descriptionBuffer), "Installation of package dependency '%s' requested", base);

    transactionId = served_state_transaction_new(&(struct served_transaction_options){
        .name = &nameBuffer[0],
        .description = &descriptionBuffer[0],
        .type = SERVED_TRANSACTION_TYPE_INSTALL,
    });

    served_state_transaction_state_new(
        transactionId, 
        &(struct state_transaction){
            .name = base,
            .channel = "stable",
        }
    );
    TXLOG_INFO(
        transaction,
        "Installing base required for %s - transaction ID %u",
        name, transactionId
    );
    served_state_unlock();
    return served_transaction_wait(transaction, SERVED_TRANSACTION_WAIT_TYPE_TRANSACTION, transactionId);
}

enum sm_action_result served_handle_state_dependencies(void* context)
{
    struct served_transaction* transaction = context;
    struct state_transaction*  state;
    char**                     names = NULL;
    char*                      path = NULL;
    char*                      base = NULL;
    struct chef_package*       package = NULL;
    sm_event_t                 event = SERVED_TX_EVENT_FAILED;

    served_state_lock();
    state = served_state_transaction(transaction->id);
    if (state == NULL) {
        TXLOG_ERROR(transaction, "Failed to load transaction state while resolving dependencies");
        served_state_unlock();
        goto cleanup;
    }

    names = utils_split_package_name(state->name);
    served_state_unlock();

    if (names == NULL) {
        TXLOG_ERROR(transaction, "Failed to split package name identifiers");
        goto cleanup;
    }

    path = utils_path_pack(names[0], names[1]);
    if (path == NULL) {
        TXLOG_ERROR(transaction,
            "Failed to construct package path for '%s/%s'",
            names[0], names[1]
        );
        goto cleanup;
    }

    // Goal: parse the package, figure out which base it uses. We then need to ensure
    // that the base of the package is installed prior to the package itself.
    if (chef_package_load(path, &package, NULL, NULL, NULL)) {
        TXLOG_ERROR(
            transaction,
            "Failed to load package %s/%s (%s): %s",
            names[0], names[1], path, strerror(errno)
        );
        goto cleanup;
    }

    // If a base is specified, ensure it is installed
    if (package->base != NULL && strlen(package->base) > 0) {
        base = utils_base_to_store_id(package->base);
        if (base == NULL) {
            TXLOG_ERROR(
                transaction,
                "Failed to allocate memory for base store ID (%s)",
                package->base
            );
            goto cleanup;
        }

        event = __ensure_base(transaction, state->name, base);
    } else {
        // No base specified, nothing to do
        TXLOG_INFO(transaction, "No package dependencies detected");
        event = SERVED_TX_EVENT_OK;
    }

cleanup:
    chef_package_free(package);
    strsplit_free(names);
    free(base);
    free(path);
    served_sm_post_event(&transaction->sm, event);
    return event == SERVED_TX_EVENT_WAIT ? SM_ACTION_WAIT : SM_ACTION_CONTINUE;
}

enum sm_action_result served_handle_state_dependencies_wait(void* context)
{
    struct served_transaction* transaction = context;

    TXLOG_INFO(transaction, "Package dependencies resolved, continuing with installation...");
    served_sm_post_event(&transaction->sm, SERVED_TX_EVENT_OK);
    return SM_ACTION_CONTINUE;
}
