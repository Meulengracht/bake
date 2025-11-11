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
#include <threads.h>
#include <time.h>
#include <vlog.h>

#include <transaction/transaction.h>
#include <transaction/sets.h>

// Runner thread state
static thrd_t g_runner_thread;
static mtx_t  g_runner_lock;
static cnd_t  g_runner_cond;
static int    g_runner_should_stop = 0;
static int    g_runner_is_running = 0;

// Transaction queues
static mtx_t       g_queue_lock;
static struct list g_active_transactions = { 0 };
static struct list g_waiting_transactions = { 0 };

// Runner tick rate (milliseconds between execute cycles)
#define RUNNER_TICK_S  0
#define RUNNER_TICK_MS 500

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

// Check if a transaction's wait condition is satisfied
static int __is_wait_satisfied(struct served_transaction* txn)
{
    switch (txn->wait.type) {
        case SERVED_TRANSACTION_WAIT_TYPE_NONE:
            return 1;
            
        case SERVED_TRANSACTION_WAIT_TYPE_TRANSACTION: {
            // Check if the transaction we're waiting for still exists
            struct list_item* i;
            
            // Check active transactions
            list_foreach(&g_active_transactions, i) {
                struct served_transaction* other = (struct served_transaction*)i;
                if (other->id == txn->wait.data.transaction_id) {
                    return 0; // Still running
                }
            }
            
            // Check waiting transactions
            list_foreach(&g_waiting_transactions, i) {
                struct served_transaction* other = (struct served_transaction*)i;
                if (other->id == txn->wait.data.transaction_id) {
                    return 0; // Still waiting
                }
            }
            
            // Transaction not found = completed
            return 1;
        }
        
        case SERVED_TRANSACTION_WAIT_TYPE_REBOOT:
            // TODO: Implement reboot detection
            // For now, assume reboot never happens
            return 0;
        
        default:
            return 0;
    }
}

// Check waiting transactions and resume those whose conditions are met
static void __process_waiting_transactions(void)
{
    struct list_item* i;
    struct list_item* next;
    
    mtx_lock(&g_queue_lock);
    list_foreach_safe(&g_waiting_transactions, i, next) {
        struct served_transaction* txn = (struct served_transaction*)i;
        
        if (__is_wait_satisfied(txn)) {
            VLOG_DEBUG("served", "Transaction %u wait satisfied, resuming\n", txn->id);
            
            // Move back to active queue
            list_remove(&g_waiting_transactions, &txn->list_header);
            list_add(&g_active_transactions, &txn->list_header);
            
            // Clear wait state
            txn->wait.type = SERVED_TRANSACTION_WAIT_TYPE_NONE;
            txn->wait.data.transaction_id = 0;
            // Will be persisted in next update
        }
    }
    mtx_unlock(&g_queue_lock);
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

    mtx_lock(&g_queue_lock);
    for (int i = 0; i < transactionsCount; i++) {
        struct served_transaction* persisted = &transactions[i];
        struct served_transaction* runtime = &runtimes[i];
        
        // Construct transaction from persisted data
        served_transaction_construct(runtime, &(struct served_transaction_options) {
            .id = persisted->id,
            .name = persisted->name,
            .description = persisted->description,
            .type = persisted->type,
            .initialState = served_sm_current_state(&persisted->sm),
            .wait = persisted->wait
        });
        
        // Add to appropriate queue based on wait state
        if (runtime->wait.type == SERVED_TRANSACTION_WAIT_TYPE_NONE) {
            list_add(&g_active_transactions, &runtime->list_header);
            VLOG_DEBUG("served", "__reconstruct_transactions_from_db: reconstructed active transaction %u (type=%d, state=%d)\n",
                       runtime->id, runtime->type, served_sm_current_state(&runtime->sm));
        } else {
            list_add(&g_waiting_transactions, &runtime->list_header);
            VLOG_DEBUG("served", "__reconstruct_transactions_from_db: reconstructed waiting transaction %u (type=%d, state=%d, wait=%d)\n",
                       runtime->id, runtime->type, served_sm_current_state(&runtime->sm), runtime->wait.type);
        }
    }
    
    mtx_unlock(&g_queue_lock);
    served_state_unlock();
    return 0;
}

void served_runner_execute(void)
{
    struct list_item* i;
    struct list_item* next;
    
    // First, check if any waiting transactions can resume
    __process_waiting_transactions();
    
    VLOG_DEBUG("served", "served_runner_execute: processing %d active, %d waiting transactions\n",
               g_active_transactions.count, g_waiting_transactions.count);
    
    // Use safe iteration since we may move transactions to waiting queue
    mtx_lock(&g_queue_lock);
    list_foreach_safe(&g_active_transactions, i, next) {
        struct served_transaction*         txn = (struct served_transaction*)i;
        sm_state_t                         oldState = served_sm_current_state(&txn->sm);
        enum served_transaction_wait_type  oldWaitType = txn->wait.type;
        enum sm_action_result              result;

        VLOG_DEBUG("served", "served_runner_execute: processing transaction %u (state=%d)\n", txn->id, oldState);

        // Execute the current state's action (only runs on state entry)
        result = served_sm_execute(&txn->sm);

        // Update state if anything changed (state or wait condition)
        if ((served_sm_current_state(&txn->sm) != oldState || txn->wait.type != oldWaitType) && 
            txn->type != SERVED_TRANSACTION_TYPE_EPHEMERAL) {
            if (served_state_transaction_update(txn) != 0) {
                VLOG_ERROR("served", "served_runner_execute: failed to update transaction %u state\n", txn->id);
            }
        }

        // Check if transaction entered wait state (state machine set txn->wait)
        if (result == SM_ACTION_WAIT) {
            VLOG_DEBUG("served", "Transaction %u entering wait state (type=%d)\n", 
                       txn->id, txn->wait.type);
            
            // Move to waiting queue
            list_remove(&g_active_transactions, &txn->list_header);
            list_add(&g_waiting_transactions, &txn->list_header);
            continue;
        }

        // Handle completion/abort
        if (result == SM_ACTION_DONE || result == SM_ACTION_ABORT) {
            VLOG_DEBUG("served", "served_runner_execute: transaction %u %s\n", 
                       txn->id, result == SM_ACTION_DONE ? "completed" : "aborted");
            // TODO: Remove from state and runner list
        }
    }
    mtx_unlock(&g_queue_lock);
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
    
    // Add to active queue (new transactions always start active)
    mtx_lock(&g_queue_lock);
    list_add(&g_active_transactions, &txn->list_header);
    mtx_unlock(&g_queue_lock);
    
    VLOG_DEBUG("served", "served_transaction_create: created transaction %u\n", transaction_id);
    return transaction_id;
}

void served_transaction_construct(struct served_transaction* transaction, struct served_transaction_options* options)
{
    transaction->id = options->id;
    transaction->name = options->name ? platform_strdup(options->name) : NULL;
    transaction->description = options->description ? platform_strdup(options->description) : NULL;
    transaction->type = options->type;
    transaction->wait = options->wait;

    served_sm_init(
        &transaction->sm,
        options->type == SERVED_TRANSACTION_TYPE_EPHEMERAL ? options->stateSet : __state_set_from_type(options->type),
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

// Runner thread main loop
static int __runner_thread_main(void* arg)
{
    struct timespec sleep_time;
    int             status;
    (void)arg;
    
    VLOG_DEBUG("served", "__runner_thread_main: runner thread started\n");
    
    status = __reconstruct_transactions_from_db();
    if (status) {
        VLOG_ERROR("served", "__runner_thread_main: failed to reconstruct transactions from database\n");
    }

    mtx_lock(&g_runner_lock);
    g_runner_is_running = 1;
    
    // Signal that we're ready
    cnd_signal(&g_runner_cond);
    mtx_unlock(&g_runner_lock);
    
    while (1) {
        // Check for stop request
        mtx_lock(&g_runner_lock);
        if (g_runner_should_stop) {
            mtx_unlock(&g_runner_lock);
            break;
        }
        mtx_unlock(&g_runner_lock);
        
        // Execute transaction runner cycle
        served_runner_execute();
        
        // Sleep for tick interval
        sleep_time.tv_sec = RUNNER_TICK_S;
        sleep_time.tv_nsec = RUNNER_TICK_MS * 1000000; // Convert ms to ns
        thrd_sleep(&sleep_time, NULL);
    }
    
    mtx_lock(&g_runner_lock);
    g_runner_is_running = 0;
    cnd_signal(&g_runner_cond);
    mtx_unlock(&g_runner_lock);
    
    VLOG_DEBUG("served", "__runner_thread_main: runner thread stopped\n");
    return 0;
}

int served_runner_start(void)
{
    int status;
    
    VLOG_TRACE("served", "served_runner_start()\n");
    
    // Initialize synchronization primitives
    if (mtx_init(&g_runner_lock, mtx_plain) != thrd_success) {
        VLOG_ERROR("served", "served_runner_start: failed to initialize mutex\n");
        return -1;
    }
    
    if (cnd_init(&g_runner_cond) != thrd_success) {
        VLOG_ERROR("served", "served_runner_start: failed to initialize condition variable\n");
        mtx_destroy(&g_runner_lock);
        return -1;
    }
    
    if (mtx_init(&g_queue_lock, mtx_plain) != thrd_success) {
        VLOG_ERROR("served", "served_runner_start: failed to initialize queue mutex\n");
        cnd_destroy(&g_runner_cond);
        mtx_destroy(&g_runner_lock);
        return -1;
    }
    
    // Reset stop flag
    g_runner_should_stop = 0;
    g_runner_is_running = 0;
    
    // Create the runner thread
    status = thrd_create(&g_runner_thread, __runner_thread_main, NULL);
    if (status != thrd_success) {
        VLOG_ERROR("served", "served_runner_start: failed to create runner thread\n");
        mtx_destroy(&g_queue_lock);
        cnd_destroy(&g_runner_cond);
        mtx_destroy(&g_runner_lock);
        return -1;
    }
    
    // Wait for the thread to signal it's running
    mtx_lock(&g_runner_lock);
    while (!g_runner_is_running) {
        cnd_wait(&g_runner_cond, &g_runner_lock);
    }
    mtx_unlock(&g_runner_lock);
    
    VLOG_DEBUG("served", "served_runner_start: runner thread is now active\n");
    return 0;
}

int served_runner_stop(void)
{
    int result;
    
    VLOG_TRACE("served", "served_runner_stop()\n");
    
    // Check if thread is running
    mtx_lock(&g_runner_lock);
    if (!g_runner_is_running) {
        mtx_unlock(&g_runner_lock);
        VLOG_DEBUG("served", "served_runner_stop: runner thread not running\n");
        return 0;
    }
    
    // Request stop
    g_runner_should_stop = 1;
    mtx_unlock(&g_runner_lock);
    
    VLOG_DEBUG("served", "served_runner_stop: waiting for runner thread to stop...\n");
    
    // Wait for thread to finish
    if (thrd_join(g_runner_thread, &result) != thrd_success) {
        VLOG_ERROR("served", "served_runner_stop: failed to join runner thread\n");
        return -1;
    }
    
    // Wait for confirmation that thread has stopped
    mtx_lock(&g_runner_lock);
    while (g_runner_is_running) {
        cnd_wait(&g_runner_cond, &g_runner_lock);
    }
    mtx_unlock(&g_runner_lock);
    
    // Cleanup synchronization primitives
    mtx_destroy(&g_queue_lock);
    cnd_destroy(&g_runner_cond);
    mtx_destroy(&g_runner_lock);
    
    VLOG_DEBUG("served", "served_runner_stop: runner thread stopped successfully\n");
    return 0;
}

int served_runner_is_running(void)
{
    int running;
    
    mtx_lock(&g_runner_lock);
    running = g_runner_is_running;
    mtx_unlock(&g_runner_lock);
    
    return running;
}
