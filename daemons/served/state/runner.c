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
#include <stdlib.h>
#include <state.h>
#include <vlog.h>

#include <transaction/transaction.h>
#include <transaction/sets.h>

static struct list g_transactions = { 0 };

static struct served_sm_state_set* __state_set_from_type(enum served_transaction_type type)
{
    struct served_sm_state_set* set = calloc(1, sizeof(struct served_sm_state_set));
    if (set == NULL) {
        VLOG_ERROR("served", "__state_set_from_type: failed to allocate state set\n");
        return NULL;
    }

    // Initialize the state set based on the transaction type
    switch (type) {
    case SERVED_TRANSACTION_TYPE_INSTALL:
        set->states = g_stateSetInstall;
        set->states_count = 14;
        break;
    case SERVED_TRANSACTION_TYPE_UNINSTALL:
        set->states = g_stateSetUninstall;
        set->states_count = 8;
        break;
    case SERVED_TRANSACTION_TYPE_UPDATE:
        set->states = g_stateSetUpdate;
        set->states_count = 18;
        break;
    default:
        VLOG_ERROR("served", "__state_set_from_type: unsupported transaction type: %d\n", type);
        free(set);
        return NULL;
    }

    return set;
}

static int __reconstruct_transactions_from_db(void)
{
    struct served_transaction* transactions;
    int                        transactionsCount;
    struct served_transaction* runtimes;
    int                        status;

    served_state_lock();
    status = served_state_get_transactions(&transactions, &transactionsCount);
    if (status) {
        VLOG_ERROR("served", "__reconstruct_transactions_from_db: failed to get transactions: %d\n", status);
        served_state_unlock();
        return -1;
    }

    runtimes = calloc(transactionsCount, sizeof(struct served_transaction));
    if (runtimes == NULL) {
        VLOG_ERROR("served", "__reconstruct_transactions_from_db: failed to allocate runtime transactions\n");
        served_state_unlock();
        return -1;
    }

    for (int i = 0; i < transactionsCount; i++) {
        struct served_transaction* persisted_txn = &transactions[i];
        struct served_transaction* runtime_txn = &runtimes[i];
        
        // Copy data from persisted transaction
        runtime_txn->id = persisted_txn->id;
        runtime_txn->name = persisted_txn->name ? platform_strdup(persisted_txn->name) : NULL;
        runtime_txn->description = persisted_txn->description ? platform_strdup(persisted_txn->description) : NULL;
        runtime_txn->type = persisted_txn->type;
        runtime_txn->state = persisted_txn->state;
        runtime_txn->wait = persisted_txn->wait;
        
        // Initialize state machine
        served_sm_init(
            &runtime_txn->sm,
            __state_set_from_type(persisted_txn->type),
            persisted_txn->state,
            runtime_txn
        );
        
        list_add(&g_transactions, &runtime_txn->list_header);
        
        VLOG_DEBUG("served", "__reconstruct_transactions_from_db: reconstructed transaction %u (type=%d, state=%d)\n",
                   runtime_txn->id, runtime_txn->type, runtime_txn->state);
    }
    
    served_state_unlock();
    return 0;
}

void served_runner_initialize(void)
{
    int status;

    status = __reconstruct_transactions_from_db();
    if (status) {
        VLOG_ERROR("served", "served_runner_initialize: failed to reconstruct transactions from database\n");
    }
}

void served_runner_execute(void)
{
    struct list_item* i;
    VLOG_DEBUG("served", "served_runner_execute: processing %d transactions\n", g_transactions.count);
    
    list_foreach(&g_transactions, i) {
        struct served_transaction* txn = (struct served_transaction*)i;
        sm_state_t                 oldState = txn->state;
        enum sm_action_result      result;
        
        VLOG_DEBUG("served", "served_runner_execute: processing transaction %u (state=%d)\n", txn->id, txn->state);
        
        result = served_sm_execute(&txn->sm);
        
        // Get the new state after execution
        txn->state = served_sm_current_state(&txn->sm);
        
        // If state changed and this is a persistent transaction, update the database
        if (txn->state != oldState && txn->type != SERVED_TRANSACTION_TYPE_EPHEMERAL) {
            served_state_lock();
            if (served_state_transaction_update(txn) != 0) {
                VLOG_ERROR("served", "served_runner_execute: failed to update transaction %u state\n", txn->id);
            }
            served_state_unlock();
        }
        
        // Handle completion/abort
        if (result == SM_ACTION_DONE || result == SM_ACTION_ABORT) {
            VLOG_DEBUG("served", "served_runner_execute: transaction %u %s\n", 
                       txn->id, result == SM_ACTION_DONE ? "completed" : "aborted");
            // TODO: Remove from state and runner list
        }
    }
}

unsigned int served_transaction_create(struct served_transaction_options* options)
{
    struct served_transaction* txn;
    unsigned int               transaction_id = 0;
    
    // For persistent transactions, create in state first
    if (options->type != SERVED_TRANSACTION_TYPE_EPHEMERAL) {
        served_state_lock();
        transaction_id = served_state_transaction_new(options);
        if (transaction_id == 0) {
            VLOG_ERROR("served", "served_transaction_create: failed to create transaction in state\n");
            served_state_unlock();
            return 0;
        }
        served_state_unlock();  // Commits transaction to database here
    }
    
    // Now create the runtime transaction wrapper
    txn = served_transaction_new(options);
    if (txn == NULL) {
        VLOG_ERROR("served", "served_transaction_create: failed to allocate transaction\n");
        return 0;
    }
    
    txn->id = transaction_id;
    list_add(&g_transactions, &txn->list_header);
    
    VLOG_DEBUG("served", "served_transaction_create: created transaction %u\n", transaction_id);
    return transaction_id;
}

void served_transaction_construct(struct served_transaction* transaction, struct served_transaction_options* options)
{
    transaction->id = options->id;
    transaction->name = options->name ? platform_strdup(options->name) : NULL;
    transaction->description = options->description ? platform_strdup(options->description) : NULL;
    transaction->type = options->type;
    transaction->state = options->initialState;
    
    served_sm_init(
        &transaction->sm,
        __state_set_from_type(options->type),
        options->initialState,
        transaction
    );
}

struct served_transaction* served_transaction_new(struct served_transaction_options* options)
{
    struct served_transaction* txn = calloc(1, sizeof(struct served_transaction));
    if (txn == NULL) {
        VLOG_ERROR("served", "served_transaction_create: failed to allocate transaction\n");
        return NULL;
    }
    served_transaction_construct(txn, options);
    return txn;
}

void served_transaction_delete(struct served_transaction* transaction)
{
    if (transaction == NULL) {
        return;
    }
    free((void*)transaction->name);
    free((void*)transaction->description);
    free(transaction);
}
