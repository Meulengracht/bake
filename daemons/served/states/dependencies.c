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
#include <runner.h>
#include <state.h>
#include <utils.h>

#include <chef/package_manifest.h>
#include <chef/platform.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static sm_event_t __ensure_base(struct served_transaction* transaction, const char* name, const char* baseName)
{
    struct state_application* application;
    struct state_application* base;
    unsigned int              transactionId;
    char                      nameBuffer[256];
    char                      descriptionBuffer[512];
    int                       applicationMissing;
    int                       baseInstalled;
    
    served_state_lock();
    application = served_state_application(name);
    base = served_state_application(baseName);
    applicationMissing = (application == NULL);
    baseInstalled = (base != NULL);
    served_state_unlock();

    if (applicationMissing) {
        TXLOG_ERROR(
            transaction,
            "Package '%s' not found in state while resolving base dependency",
            name
        );
        return SERVED_TX_EVENT_FAILED;
    }

    if (baseInstalled) {
        TXLOG_INFO(transaction, "Base for %s already installed", name);
        return SERVED_TX_EVENT_OK;
    }

    // schedule installation
    snprintf(nameBuffer, sizeof(nameBuffer), "Install dependency (%s)", baseName);
    snprintf(descriptionBuffer, sizeof(descriptionBuffer), "Installation of package dependency '%s' requested", baseName);

    transactionId = served_transaction_create_locked(
        &(struct served_transaction_options){
            .name = &nameBuffer[0],
            .description = &descriptionBuffer[0],
            .type = SERVED_TRANSACTION_TYPE_INSTALL,
        },
        &(struct state_transaction){
            .name = baseName,
            .channel = "stable",
        }
    );
    if (transactionId == 0) {
        TXLOG_ERROR(
            transaction,
            "Failed to schedule base installation for %s",
            baseName
        );
        return SERVED_TX_EVENT_FAILED;
    }

    TXLOG_INFO(
        transaction,
        "Installing base required for %s - transaction ID %u",
        name, transactionId
    );
    return served_transaction_wait(transaction, SERVED_TRANSACTION_WAIT_TYPE_TRANSACTION, transactionId);
}

enum sm_action_result served_handle_state_dependencies(void* context)
{
    struct served_transaction* transaction = context;
    struct state_transaction*  state;
    char*                      packageName = NULL;
    char**                     names = NULL;
    char*                      path = NULL;
    char*                      base = NULL;
    struct chef_package_manifest* manifest = NULL;
    sm_event_t                 event = SERVED_TX_EVENT_FAILED;
    int                        revision;

    served_state_lock();
    state = served_state_transaction(transaction->id);
    if (state == NULL) {
        served_state_unlock();
        TXLOG_ERROR(transaction, "Failed to load transaction state while resolving dependencies");
        goto cleanup;
    }

    packageName = state->name ? platform_strdup(state->name) : NULL;
    revision = state->revision;
    served_state_unlock();

    if (packageName == NULL) {
        TXLOG_ERROR(transaction, "Failed to copy package name from transaction state");
        goto cleanup;
    }

    names = utils_split_package_name(packageName);

    if (names == NULL) {
        TXLOG_ERROR(transaction, "Failed to split package name identifiers");
        goto cleanup;
    }

    path = utils_path_pack(names[0], names[1], revision);
    if (path == NULL) {
        TXLOG_ERROR(transaction,
            "Failed to construct package path for '%s/%s'",
            names[0], names[1]
        );
        goto cleanup;
    }

    // Goal: parse the package, figure out which base it uses. We then need to ensure
    // that the base of the package is installed prior to the package itself.
    if (chef_package_manifest_load(path, &manifest) != 0) {
        TXLOG_ERROR(
            transaction,
            "Failed to load package %s/%s (%s): %s",
            names[0], names[1], path, strerror(errno)
        );
        goto cleanup;
    }

    // If a base is specified, ensure it is installed
    if (manifest->base != NULL && strlen(manifest->base) > 0) {
        // Two types of automatic bases are supported:
        // Bases from the same identity (e.g. publisher "foo" can have package "foo/base" as a base for "foo/bar")
        // Bases from the official identity (vali/base)
        const char* possibleBaseIdentities[3] = {
            names[0],
            "vali",
            NULL
        };

        for (int i = 0; possibleBaseIdentities[i] != NULL; i++) {
            base = utils_base_to_store_id(possibleBaseIdentities[i], manifest->base);
            if (base == NULL) {
                TXLOG_ERROR(
                    transaction,
                    "Failed to allocate memory for base store ID (%s)",
                    manifest->base
                );
                break;
            }

            event = __ensure_base(transaction, packageName, base);
            free(base);
            if (event == SERVED_TX_EVENT_OK) {
                break;
            }
        }

    } else {
        // No base specified, nothing to do
        TXLOG_INFO(transaction, "No package dependencies detected");
        event = SERVED_TX_EVENT_OK;
    }

cleanup:
    chef_package_manifest_free(manifest);
    strsplit_free(names);
    free(packageName);
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
