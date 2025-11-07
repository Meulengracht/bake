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

static struct served_sm_state_set* __state_set_from_type(enum state_transaction_type type)
{
    struct served_sm_state_set* set = calloc(1, sizeof(struct served_sm_state_set));
    if (set == NULL) {
        VLOG_ERROR("served", "__state_set_from_type: failed to allocate state set\n");
        return NULL;
    }

    // Initialize the state set based on the transaction type
    switch (type) {
    case STATE_TRANSACTION_TYPE_INSTALL:
        set->states = g_stateSetInstall;
        set->states_count = 14;
        break;
    case STATE_TRANSACTION_TYPE_UNINSTALL:
        set->states = g_stateSetUninstall;
        set->states_count = 8;
        break;
    case STATE_TRANSACTION_TYPE_UPDATE:
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
    struct state_transaction* transactions;
    int                       transactionsCount;
    int                       status;

    status = served_state_get_transaction_states(&transactions, &transactionsCount);
    if (status) {
        VLOG_ERROR("served", "__reconstruct_transactions_from_db: failed to get transactions: %d\n", status);
        return -1;
    }

    for (int i = 0; i < transactionsCount; i++) {
        struct state_transaction*  txnState = &transactions[i];
        status = served_transaction_create(
            txnState->id,
            __state_set_from_type(txnState->type),
            txnState->state,
            0 // transactions loaded from db are never ephemeral
        );
    }
    return 0;
}

void served_state_execute(void)
{
    struct list_item* i;
    VLOG_DEBUG("served", "served_state_execute: processing %d transactions\n", g_transactions.count);
    list_foreach(&g_transactions, i) {
        struct served_transaction* txn = (struct served_transaction*)i;
        VLOG_DEBUG("served", "served_state_execute: processing transaction %d\n", txn->id);
        served_sm_execute(&txn->sm);
    }
}

unsigned int served_transaction_create(struct served_transaction_options* options)
{
    struct served_transaction* txn = calloc(1, sizeof(struct served_transaction));
    if (txn == NULL) {
        VLOG_ERROR("served", "served_transaction_create: failed to allocate transaction\n");
        return -1;
    }

    txn->id = id;
    txn->ephemeral = ephemeral;

    served_sm_init(&txn->sm, stateSet, (sm_state_t)initialState, txn);
    list_add(&g_transactions, &txn->list_header);
    return 0;
}

void served_transaction_delete(struct served_transaction* transaction)
{
    if (transaction == NULL) {
        return;
    }
    list_remove(&g_transactions, &transaction->list_header);
    free(transaction);
}
