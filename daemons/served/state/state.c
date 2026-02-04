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

// Served state implementation using sqlite as a backend. The state will allow for storing
// application and transaction states, as well as the current state of the backend to be resilient
// against crashes and restarts.
// 
// The state database schema will be as follows:
// 
// Table: applications
// Columns:
// - id (INTEGER PRIMARY KEY AUTOINCREMENT)
// - name (TEXT)
// 
static const char* g_applicationTableSQL = 
    "CREATE TABLE IF NOT EXISTS applications ("
    "id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "name TEXT UNIQUE NOT NULL"
    ");";

// Table: commands
// Columns:
// - id (INTEGER PRIMARY KEY AUTOINCREMENT)
// - application_id (INTEGER, FOREIGN KEY to applications.id)
// - name (TEXT)
// - path (TEXT)
// - arguments (TEXT)
// - type (INTEGER)
// 
static const char* g_commandsTableSQL = 
    "CREATE TABLE IF NOT EXISTS commands ("
    "id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "application_id INTEGER,"
    "name TEXT NOT NULL,"
    "path TEXT,"
    "arguments TEXT,"
    "type INTEGER,"
    "FOREIGN KEY(application_id) REFERENCES applications(id) ON DELETE CASCADE"
    ");";

// Table: revisions
// Columns:
// - id (INTEGER PRIMARY KEY AUTOINCREMENT)
// - application_id (INTEGER, FOREIGN KEY to applications.id)
// - channel (TEXT)
// - major (INTEGER)
// - minor (INTEGER)
// - patch (INTEGER)
// - revision (INTEGER)
// - tag (TEXT)
// - size (INTEGER)
// - created (TEXT)
// 
static const char* g_revisionsTableSQL = 
    "CREATE TABLE IF NOT EXISTS revisions ("
    "id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "application_id INTEGER,"
    "channel TEXT,"
    "major INTEGER,"
    "minor INTEGER,"
    "patch INTEGER,"
    "revision INTEGER,"
    "tag TEXT,"
    "size INTEGER,"
    "created TEXT,"
    "FOREIGN KEY(application_id) REFERENCES applications(id) ON DELETE CASCADE"
    ");";

// Table: transactions
// Columns:
// - id (INTEGER PRIMARY KEY)
// - type (INTEGER)
// - state (INTEGER)
// - flags (INTEGER)
// - name (TEXT)
// - description (TEXT)
// - wait_type (INTEGER)
// - wait_data (INTEGER)
// 
static const char* g_transactionsTableSQL = 
    "CREATE TABLE IF NOT EXISTS transactions ("
    "id INTEGER PRIMARY KEY,"
    "type INTEGER NOT NULL,"
    "flags INTEGER NOT NULL,"
    "state INTEGER NOT NULL,"
    "name TEXT,"
    "description TEXT,"
    "wait_type INTEGER DEFAULT 0,"
    "wait_data INTEGER DEFAULT 0,"
    "created_at INTEGER NOT NULL DEFAULT (strftime('%s', 'now')),"
    "completed_at INTEGER DEFAULT NULL"
    ");";

static const char* g_transactionsStateTableSQL = 
    "CREATE TABLE IF NOT EXISTS transactions_state ("
    "id INTEGER PRIMARY KEY,"
    "transaction_id INTEGER NOT NULL,"
    "name TEXT,"
    "channel TEXT,"
    "revision INTEGER,"
    "FOREIGN KEY(transaction_id) REFERENCES transactions(id) ON DELETE CASCADE"
    ");";

static const char* g_transactionLogsTableSQL =
    "CREATE TABLE IF NOT EXISTS transaction_logs ("
    "id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "transaction_id INTEGER NOT NULL,"
    "level INTEGER NOT NULL,"
    "timestamp INTEGER NOT NULL,"
    "state INTEGER NOT NULL,"
    "message TEXT NOT NULL,"
    "FOREIGN KEY(transaction_id) REFERENCES transactions(id) ON DELETE CASCADE"
    ");";

// The state implementation will provide functions to add, remove, and query applications and transactions.
// The state also will allow for transactional changes to ensure consistency across multiple operations, and
// to ensure resilience against crashes and restarts

#include <errno.h>
#if defined(__linux__)
#include <linux/limits.h>
#else
#include <limits.h>
#endif
#include <chef/platform.h>
#include <chef/package.h>
#include <chef/list.h>
#include <sqlite3.h>
#include <state.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <utils.h>
#include <vlog.h>

// Deferred operation types
enum deferred_operation_type {
    DEFERRED_OP_ADD_APPLICATION,
    DEFERRED_OP_REMOVE_APPLICATION,
    DEFERRED_OP_ADD_TRANSACTION,
    DEFERRED_OP_UPDATE_TRANSACTION,
    DEFERRED_OP_ADD_TRANSACTION_STATE,
    DEFERRED_OP_UPDATE_TRANSACTION_STATE,
    DEFERRED_OP_COMPLETE_TRANSACTION,
    DEFERRED_OP_ADD_TRANSACTION_LOG
};

// Deferred operation entry
struct deferred_operation {
    struct list_item             list_node;
    enum deferred_operation_type type;
    union {
        struct {
            struct state_application* application;
        } add_app;
        struct {
            char* application_name;
        } remove_app;
        struct {
            struct served_transaction* transaction;
        } add_tx;
        struct {
            struct served_transaction* transaction;
        } update_tx;
        struct {
            unsigned int              transaction_id;
            struct state_transaction* transaction;
        } add_tx_state;
        struct {
            struct state_transaction* transaction;
        } update_tx_state;
        struct {
            unsigned int transaction_id;
        } complete_tx;
        struct {
            unsigned int                      transaction_id;
            enum served_transaction_log_level level;
            time_t                            timestamp;
            sm_state_t                        state;
            char*                             message;
        } add_log;
    } data;
    struct deferred_operation* next;
};

static struct deferred_operation* __deferred_operation_new(enum deferred_operation_type type)
{
    struct deferred_operation* op = malloc(sizeof(struct deferred_operation));
    if (op == NULL) {
        return NULL;
    }
    memset(op, 0, sizeof(struct deferred_operation));
    op->type = type;
    return op;
}

struct __state {
    struct served_transaction* transactions;
    int                        transactions_count;
    struct state_transaction*  transaction_states;
    int                        transaction_state_count;
    struct state_application*  applications_states;
    int                        applications_states_count;

    sqlite3* database;
    mtx_t    lock;
    int      lock_count;
    
    // Transaction ID management
    unsigned int next_transaction_id;
    
    // Deferred operation queue
    struct list deferred_ops;
};

// Database connection and state management
static struct __state* g_state = NULL;

static struct __state* __state_new(void)
{
    struct __state* state;

    state = malloc(sizeof(struct __state));
    if (state == NULL) {
        return NULL;
    }
    memset(state, 0, sizeof(struct __state));

    if (mtx_init(&state->lock, mtx_plain) != thrd_success) {
        VLOG_ERROR("served", "__state_new: failed to initialize mutex\n");
        free((void*)state);
        return NULL;
    }
    return state;
}

// Deferred operation queue management
static void __deferred_operation_free(struct deferred_operation* op)
{
    if (op == NULL) {
        return;
    }

    switch (op->type) {
        case DEFERRED_OP_REMOVE_APPLICATION:
            free(op->data.remove_app.application_name);
            break;
        default:
            // Other operations don't own their data
            break;
    }
    free(op);
}

static void __clear_deferred_operations(struct __state* state)
{
    list_destroy(&state->deferred_ops, (void (*)(void*))__deferred_operation_free);
}

static void __enqueue_deferred_operation(struct __state* state, struct deferred_operation* op)
{
    list_add(&state->deferred_ops, &op->list_node);
}

static void __state_application_delete(struct state_application* application)
{
    if (application == NULL) {
        return;
    }

    if (application->commands) {
        for (int i = 0; i < application->commands_count; i++) {
            free((void*)application->commands[i].name);
            free((void*)application->commands[i].path);
            free((void*)application->commands[i].arguments);
        }
        free((void*)application->commands);
    }
    if (application->revisions) {
        for (int i = 0; i < application->revisions_count; i++) {
            free((void*)application->revisions[i].tracking_channel);
            if (application->revisions[i].version) {
                chef_version_free(application->revisions[i].version);
            }
        }
        free((void*)application->revisions);
    }
    free((void*)application->name);
}

static void __state_transaction_delete(struct state_transaction* transaction)
{
    if (transaction == NULL) {
        return;
    }

    free((void*)transaction->name);
    free((void*)transaction->channel);
}

static void __served_transaction_delete(struct served_transaction* transaction)
{
    if (transaction == NULL) {
        return;
    }

    free((void*)transaction->name);
    free((void*)transaction->description);
}

static void __state_destroy(struct __state* state)
{
    if (state == NULL) {
        return;
    }

    for (int i = 0; i < state->applications_states_count; i++) {
        __state_application_delete(&state->applications_states[i]);
    }
    free((void*)state->applications_states);

    for (int i = 0; i < state->transactions_count; i++) {
        __served_transaction_delete(&state->transactions[i]);
    }
    free((void*)state->transactions);

    for (int i = 0; i < state->transaction_state_count; i++) {
        __state_transaction_delete(&state->transaction_states[i]);
    }
    free((void*)state->transaction_states);

    __clear_deferred_operations(state);
    mtx_destroy(&state->lock);
    free((void*)state);
}

static int __create_database_schema(sqlite3* db)
{
    char* errMsg = NULL;
    int   status;

    status = sqlite3_exec(db, g_applicationTableSQL, NULL, NULL, &errMsg);
    if (status != SQLITE_OK) {
        VLOG_ERROR("served", "__create_database_schema: failed to create applications table: %s\n", errMsg);
        sqlite3_free(errMsg);
        return status;
    }

    status = sqlite3_exec(db, g_commandsTableSQL, NULL, NULL, &errMsg);
    if (status != SQLITE_OK) {
        VLOG_ERROR("served", "__create_database_schema: failed to create commands table: %s\n", errMsg);
        sqlite3_free(errMsg);
        return status;
    }

    status = sqlite3_exec(db, g_revisionsTableSQL, NULL, NULL, &errMsg);
    if (status != SQLITE_OK) {
        VLOG_ERROR("served", "__create_database_schema: failed to create revisions table: %s\n", errMsg);
        sqlite3_free(errMsg);
        return status;
    }

    status = sqlite3_exec(db, g_transactionsTableSQL, NULL, NULL, &errMsg);
    if (status != SQLITE_OK) {
        VLOG_ERROR("served", "__create_database_schema: failed to create transactions table: %s\n", errMsg);
        sqlite3_free(errMsg);
        return status;
    }

    status = sqlite3_exec(db, g_transactionsStateTableSQL, NULL, NULL, &errMsg);
    if (status != SQLITE_OK) {
        VLOG_ERROR("served", "__create_database_schema: failed to create transactions_state table: %s\n", errMsg);
        sqlite3_free(errMsg);
        return status;
    }

    status = sqlite3_exec(db, g_transactionLogsTableSQL, NULL, NULL, &errMsg);
    if (status != SQLITE_OK) {
        VLOG_ERROR("served", "__create_database_schema: failed to create transaction_logs table: %s\n", errMsg);
        sqlite3_free(errMsg);
        return status;
    }

    return 0;
}

static int __get_application_row_count(struct __state* state)
{
    const char*   query = "SELECT COUNT(*) FROM applications;";
    sqlite3_stmt* stmt;
    int           status;
    int           count = 0;

    status = sqlite3_prepare_v2(state->database, query, -1, &stmt, NULL);
    if (status != SQLITE_OK) {
        VLOG_ERROR("served", "__get_application_row_count: failed to prepare statement: %s\n", sqlite3_errmsg(state->database));
        return -1;
    }

    if ((status = sqlite3_step(stmt)) == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
    } else {
        VLOG_ERROR("served", "__get_application_row_count: failed to step statement: %s\n", sqlite3_errmsg(state->database));
        count = -1;
    }

    sqlite3_finalize(stmt);
    return count;
}

static int __application_from_stmt(sqlite3_stmt* stmt, struct state_application* application)
{
    const char* name = (const char*)sqlite3_column_text(stmt, 0);
    if (name) {
        application->name = platform_strdup(name);
        if (application->name == NULL) {
            VLOG_ERROR("served", "__application_from_stmt: failed to duplicate application name\n");
            return -1;
        }
    }
    return 0;
}

static int __load_commands_for_application(struct __state* state, const char* app_name, struct state_application* application)
{
    const char* query = 
        "SELECT c.name, c.path, c.arguments, c.type "
        "FROM applications a "
        "JOIN commands c ON a.id = c.application_id "
        "WHERE a.name = ? "
        "ORDER BY c.id";

    sqlite3_stmt* stmt;
    int status = sqlite3_prepare_v2(state->database, query, -1, &stmt, NULL);
    if (status != SQLITE_OK) {
        VLOG_ERROR("served", "__load_commands_for_application: failed to prepare statement: %s\n", sqlite3_errmsg(state->database));
        return -1;
    }

    sqlite3_bind_text(stmt, 1, app_name, -1, SQLITE_STATIC);

    // Count commands first
    int command_count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        command_count++;
    }

    if (command_count == 0) {
        sqlite3_finalize(stmt);
        application->commands = NULL;
        application->commands_count = 0;
        return 0; // No commands, which is OK
    }

    // Allocate commands array
    application->commands = calloc(command_count, sizeof(struct state_application_command));
    if (!application->commands) {
        VLOG_ERROR("served", "__load_commands_for_application: failed to allocate commands array\n");
        sqlite3_finalize(stmt);
        return -1;
    }
    application->commands_count = command_count;

    // Reset and re-execute to populate commands
    sqlite3_reset(stmt);
    sqlite3_bind_text(stmt, 1, app_name, -1, SQLITE_STATIC);

    int i = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW && i < command_count) {
        const char* cmd_name = (const char*)sqlite3_column_text(stmt, 0);
        const char* cmd_path = (const char*)sqlite3_column_text(stmt, 1);
        const char* cmd_args = (const char*)sqlite3_column_text(stmt, 2);
        int cmd_type = sqlite3_column_int(stmt, 3);
        
        // Safely copy strings with null checks and error handling
        if (cmd_name) {
            application->commands[i].name = platform_strdup(cmd_name);
            if (!application->commands[i].name) {
                VLOG_ERROR("served", "__load_commands_for_application: failed to allocate command name\n");
                sqlite3_finalize(stmt);
                return -1;
            }
        }
        if (cmd_path) {
            application->commands[i].path = platform_strdup(cmd_path);
            if (!application->commands[i].path) {
                VLOG_ERROR("served", "__load_commands_for_application: failed to allocate command path\n");
                sqlite3_finalize(stmt);
                return -1;
            }
        }
        if (cmd_args) {
            application->commands[i].arguments = platform_strdup(cmd_args);
            if (!application->commands[i].arguments) {
                VLOG_ERROR("served", "__load_commands_for_application: failed to allocate command arguments\n");
                sqlite3_finalize(stmt);
                return -1;
            }
        }
        application->commands[i].type = (enum chef_command_type)cmd_type;
        application->commands[i].pid = 0; // Not running initially

        i++;
    }

    sqlite3_finalize(stmt);
    VLOG_DEBUG("served", "__load_commands_for_application: loaded %d commands for application '%s'\n", command_count, app_name);
    return 0;
}

static int __load_revisions_for_application(struct __state* state, const char* app_name, struct state_application* application)
{
    const char* query = 
        "SELECT r.channel, r.major, r.minor, r.patch, r.revision, r.tag, r.size, r.created "
        "FROM applications a "
        "JOIN revisions r ON a.id = r.application_id "
        "WHERE a.name = ? "
        "ORDER BY r.id";

    sqlite3_stmt* stmt;
    int status = sqlite3_prepare_v2(state->database, query, -1, &stmt, NULL);
    if (status != SQLITE_OK) {
        VLOG_ERROR("served", "__load_revisions_for_application: failed to prepare statement: %s\n", sqlite3_errmsg(state->database));
        return -1;
    }

    sqlite3_bind_text(stmt, 1, app_name, -1, SQLITE_STATIC);

    // Count revisions first
    int revision_count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        revision_count++;
    }

    if (revision_count == 0) {
        sqlite3_finalize(stmt);
        application->revisions = NULL;
        application->revisions_count = 0;
        return 0; // No revisions, which is OK
    }

    // Allocate revisions array
    application->revisions = malloc(sizeof(struct state_application_revision) * revision_count);
    if (!application->revisions) {
        VLOG_ERROR("served", "__load_revisions_for_application: failed to allocate revisions array\n");
        sqlite3_finalize(stmt);
        return -1;
    }
    application->revisions_count = revision_count;

    // Reset and re-execute to populate revisions
    sqlite3_reset(stmt);
    sqlite3_bind_text(stmt, 1, app_name, -1, SQLITE_STATIC);

    int i = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW && i < revision_count) {
        const char* channel = (const char*)sqlite3_column_text(stmt, 0);
        int major = sqlite3_column_int(stmt, 1);
        int minor = sqlite3_column_int(stmt, 2);
        int patch = sqlite3_column_int(stmt, 3);
        int revision = sqlite3_column_int(stmt, 4);
        const char* tag = (const char*)sqlite3_column_text(stmt, 5);
        long long size = sqlite3_column_int64(stmt, 6);
        const char* created = (const char*)sqlite3_column_text(stmt, 7);

        // Initialize revision structure
        memset(&application->revisions[i], 0, sizeof(struct state_application_revision));
        
        // Set tracking channel
        application->revisions[i].tracking_channel = channel ? platform_strdup(channel) : platform_strdup("stable");

        // Allocate and populate version structure
        application->revisions[i].version = malloc(sizeof(struct chef_version));
        if (application->revisions[i].version) {
            memset(application->revisions[i].version, 0, sizeof(struct chef_version));
            application->revisions[i].version->major = major;
            application->revisions[i].version->minor = minor;
            application->revisions[i].version->patch = patch;
            application->revisions[i].version->revision = revision;
            application->revisions[i].version->tag = tag ? platform_strdup(tag) : NULL;
            application->revisions[i].version->size = size;
            application->revisions[i].version->created = created ? platform_strdup(created) : NULL;
        } else {
            VLOG_ERROR("served", "__load_revisions_for_application: failed to allocate version for revision %d\n", i);
            free((void*)application->revisions[i].tracking_channel);
            // Continue with other revisions
        }

        i++;
    }

    sqlite3_finalize(stmt);
    VLOG_DEBUG("served", "__load_revisions_for_application: loaded %d revisions for application '%s'\n", revision_count, app_name);
    return 0;
}

static int __load_applications_from_db(struct __state* state)
{
    const char* query = 
        "SELECT name "
        "FROM applications "
        "ORDER BY name";

    sqlite3_stmt* stmt;
    int           status;
    int           count;

    count = __get_application_row_count(state);
    if (count < 0) {
        VLOG_ERROR("served", "__load_applications_from_db: failed to get application row count\n");
        return -1;
    }

    if (count == 0) {
        state->applications_states = NULL;
        state->applications_states_count = 0;
        return 0;
    }

    state->applications_states = calloc(count, sizeof(struct state_application));
    if (state->applications_states == NULL) {
        VLOG_ERROR("served", "__load_applications_from_db: failed to allocate applications array\n");
        return -1;
    }
    state->applications_states_count = 0;

    status = sqlite3_prepare_v2(state->database, query, -1, &stmt, NULL);
    if (status != SQLITE_OK) {
        VLOG_ERROR("served", "__load_applications_from_db: failed to prepare statement: %s\n", sqlite3_errmsg(state->database));
        return -1;
    }

    while ((status = sqlite3_step(stmt)) == SQLITE_ROW) {
        struct state_application* app = &state->applications_states[state->applications_states_count];

        if (__application_from_stmt(stmt, app) != 0) {
            VLOG_ERROR("served", "__load_applications_from_db: failed to parse application from statement\n");
            sqlite3_finalize(stmt);
            return -1;
        }

        if (__load_commands_for_application(state, app->name, app) != 0) {
            VLOG_ERROR("served", "__load_applications_from_db: failed to load commands for application '%s'\n", app->name);
            sqlite3_finalize(stmt);
            return -1;
        }

        if (__load_revisions_for_application(state, app->name, app) != 0) {
            VLOG_ERROR("served", "__load_applications_from_db: failed to load revisions for application '%s'\n", app->name);
            sqlite3_finalize(stmt);
            return -1;
        }

        state->applications_states_count++;
    }

    sqlite3_finalize(stmt);
    VLOG_DEBUG("served", "__load_applications_from_db: loaded %d applications\n", state->applications_states_count);
    return 0;
}

static int __get_transaction_row_count(struct __state* state)
{
    const char*   query = "SELECT COUNT(*) FROM transactions;";
    sqlite3_stmt* stmt;
    int           status;
    int           count = 0;

    status = sqlite3_prepare_v2(state->database, query, -1, &stmt, NULL);
    if (status != SQLITE_OK) {
        VLOG_ERROR("served", "__get_transaction_row_count: failed to prepare statement: %s\n", sqlite3_errmsg(state->database));
        return -1;
    }

    if ((status = sqlite3_step(stmt)) == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
    } else {
        VLOG_ERROR("served", "__get_transaction_row_count: failed to step statement: %s\n", sqlite3_errmsg(state->database));
        count = -1;
    }

    sqlite3_finalize(stmt);
    return count;
}

static int __load_transaction_states_from_db(struct __state* state)
{
    const char* query = 
        "SELECT t.id, t.type, t.state, t.flags, ts.name, ts.channel, ts.revision "
        "FROM transactions t "
        "LEFT JOIN transactions_state ts ON t.id = ts.transaction_id "
        "ORDER BY t.id";

    sqlite3_stmt* stmt;
    int status = sqlite3_prepare_v2(state->database, query, -1, &stmt, NULL);
    if (status != SQLITE_OK) {
        VLOG_ERROR("served", "__load_transaction_states_from_db: failed to prepare statement: %s\n", sqlite3_errmsg(state->database));
        return -1;
    }

    // Count transactions first
    int transaction_count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        transaction_count++;
    }

    if (transaction_count == 0) {
        sqlite3_finalize(stmt);
        state->transaction_states = NULL;
        state->transaction_state_count = 0;
        return 0; // No transactions, which is OK
    }

    // Allocate transactions array
    state->transaction_states = malloc(sizeof(struct state_transaction) * transaction_count);
    if (!state->transaction_states) {
        VLOG_ERROR("served", "__load_transaction_states_from_db: failed to allocate transactions array\n");
        sqlite3_finalize(stmt);
        return -1;
    }
    state->transaction_state_count = transaction_count;

    // Reset and re-execute to populate transactions
    sqlite3_reset(stmt);

    int i = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW && i < transaction_count) {
        unsigned int id = (unsigned int)sqlite3_column_int(stmt, 0);
        const char* name = (const char*)sqlite3_column_text(stmt, 4);
        const char* channel = (const char*)sqlite3_column_text(stmt, 5);
        int revision = sqlite3_column_int(stmt, 6);

        // Initialize transaction structure
        memset(&state->transaction_states[i], 0, sizeof(struct state_transaction));
        
        state->transaction_states[i].id = id;
        state->transaction_states[i].name = name ? platform_strdup(name) : NULL;
        state->transaction_states[i].channel = channel ? platform_strdup(channel) : NULL;
        state->transaction_states[i].revision = revision;

        i++;
    }

    sqlite3_finalize(stmt);
    VLOG_DEBUG("served", "__load_transaction_states_from_db: loaded %d transactions\n", transaction_count);
    return 0;
}

static int __load_transactions_from_db(struct __state* state)
{
    const char* query = 
        "SELECT id, type, state, flags, name, description, wait_type, wait_data, created_at, completed_at "
        "FROM transactions "
        "ORDER BY id";

    sqlite3_stmt* stmt;
    int status = sqlite3_prepare_v2(state->database, query, -1, &stmt, NULL);
    if (status != SQLITE_OK) {
        VLOG_ERROR("served", "__load_transactions_from_db: failed to prepare statement: %s\n", sqlite3_errmsg(state->database));
        return -1;
    }

    // Count transactions first
    int transaction_count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        transaction_count++;
    }

    if (transaction_count == 0) {
        sqlite3_finalize(stmt);
        state->transactions = NULL;
        state->transactions_count = 0;
        return 0; // No transactions, which is OK
    }

    // Allocate transactions array
    state->transactions = malloc(sizeof(struct served_transaction) * transaction_count);
    if (!state->transactions) {
        VLOG_ERROR("served", "__load_transactions_from_db: failed to allocate transactions array\n");
        sqlite3_finalize(stmt);
        return -1;
    }
    state->transactions_count = transaction_count;

    // Reset and re-execute to populate transactions
    sqlite3_reset(stmt);

    int i = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW && i < transaction_count) {
        unsigned int id = (unsigned int)sqlite3_column_int(stmt, 0);
        int type = sqlite3_column_int(stmt, 1);
        int storedState = sqlite3_column_int(stmt, 2);
        const char* name = (const char*)sqlite3_column_text(stmt, 4);
        const char* description = (const char*)sqlite3_column_text(stmt, 5);
        int waitType = sqlite3_column_int(stmt, 6);
        unsigned int waitData = (unsigned int)sqlite3_column_int(stmt, 7);
        time_t createdAt = (time_t)sqlite3_column_int64(stmt, 8);
        time_t completedAt = sqlite3_column_type(stmt, 9) == SQLITE_NULL ? 0 : (time_t)sqlite3_column_int64(stmt, 9);

        served_transaction_construct(&state->transactions[i++], &(struct served_transaction_options) {
            .id = id,
            .type = type,
            .initialState = storedState,
            .name = name,
            .description = description,
            .wait = (struct served_transaction_wait) {
                .type = waitType,
                .data = {
                    .transaction_id = waitData
                }
            }
        });
        
        // Set timestamps after construction
        state->transactions[i-1].created_at = createdAt;
        state->transactions[i-1].completed_at = completedAt;
    }

    sqlite3_finalize(stmt);
    VLOG_DEBUG("served", "__load_transactions_from_db: loaded %d transactions\n", transaction_count);
    return 0;
}

static int __load_transaction_logs_from_db(struct __state* state)
{
    const char* query = 
        "SELECT transaction_id, level, timestamp, state, message "
        "FROM transaction_logs "
        "ORDER BY transaction_id, id";

    sqlite3_stmt* stmt;
    int status = sqlite3_prepare_v2(state->database, query, -1, &stmt, NULL);
    if (status != SQLITE_OK) {
        VLOG_ERROR("served", "__load_transaction_logs_from_db: failed to prepare statement: %s\n", sqlite3_errmsg(state->database));
        return -1;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        unsigned int transaction_id = (unsigned int)sqlite3_column_int(stmt, 0);
        int level = sqlite3_column_int(stmt, 1);
        time_t timestamp = (time_t)sqlite3_column_int64(stmt, 2);
        int txstate = sqlite3_column_int(stmt, 3);
        const char* message = (const char*)sqlite3_column_text(stmt, 4);

        // Find the transaction this log belongs to
        struct served_transaction* tx = NULL;
        for (int i = 0; i < state->transactions_count; i++) {
            if (state->transactions[i].id == transaction_id) {
                tx = &state->transactions[i];
                break;
            }
        }

        if (tx == NULL) {
            VLOG_WARNING("served", "__load_transaction_logs_from_db: log for unknown transaction %u\n", transaction_id);
            continue;
        }

        // Find the state_transaction for this runtime transaction
        struct state_transaction* state_tx = NULL;
        for (int i = 0; i < state->transaction_state_count; i++) {
            if (state->transaction_states[i].id == transaction_id) {
                state_tx = &state->transaction_states[i];
                break;
            }
        }

        if (state_tx == NULL) {
            VLOG_WARNING("served", "__load_transaction_logs_from_db: no state for transaction %u\n", transaction_id);
            continue;
        }

        // Add log entry to state_transaction
        struct state_transaction_log* new_logs = realloc(state_tx->logs, 
            sizeof(struct state_transaction_log) * (state_tx->logs_count + 1));
        if (new_logs == NULL) {
            VLOG_ERROR("served", "__load_transaction_logs_from_db: failed to allocate log entry\n");
            continue;
        }

        state_tx->logs = new_logs;
        struct state_transaction_log* log = &state_tx->logs[state_tx->logs_count++];
        log->level = level;
        log->timestamp = timestamp;
        log->state = txstate;
        strncpy(log->message, message, sizeof(log->message) - 1);
        log->message[sizeof(log->message) - 1] = '\0';
    }

    sqlite3_finalize(stmt);
    return 0;
}

static int __initialize_transaction_id_counter(struct __state* state)
{
    const char*   query = "SELECT MAX(id) FROM transactions;";
    sqlite3_stmt* stmt;
    int           status;
    unsigned int  max_id = 0;

    status = sqlite3_prepare_v2(state->database, query, -1, &stmt, NULL);
    if (status != SQLITE_OK) {
        VLOG_ERROR("served", "__initialize_transaction_id_counter: failed to prepare statement: %s\n", sqlite3_errmsg(state->database));
        return -1;
    }

    if ((status = sqlite3_step(stmt)) == SQLITE_ROW) {
        // Check if result is NULL (no transactions exist)
        if (sqlite3_column_type(stmt, 0) != SQLITE_NULL) {
            max_id = (unsigned int)sqlite3_column_int(stmt, 0);
        }
    } else if (status != SQLITE_DONE) {
        VLOG_ERROR("served", "__initialize_transaction_id_counter: failed to step statement: %s\n", sqlite3_errmsg(state->database));
        sqlite3_finalize(stmt);
        return -1;
    }

    sqlite3_finalize(stmt);
    
    // Start from max_id + 1
    state->next_transaction_id = max_id + 1;
    VLOG_DEBUG("served", "__initialize_transaction_id_counter: initialized to %u\n", state->next_transaction_id);
    return 0;
}

int served_state_load(void)
{
    int         status;
    const char* path = utils_path_state_db();
    VLOG_DEBUG("served", "served_state_load(path=%s)\n", path);

    g_state = __state_new();
    if (g_state == NULL) {
        VLOG_ERROR("served", "served_state_load: failed to allocate state\n");
        return -1;
    }

    status = sqlite3_open(path, &g_state->database);
    if (status != SQLITE_OK) {
        VLOG_ERROR("served", "served_state_load: failed to open database: %s\n", sqlite3_errmsg(g_state->database));
        __state_destroy(g_state);
        g_state = NULL;
        return -1;
    }

    // Create database schema if it doesn't exist
    if (__create_database_schema(g_state->database) != 0) {
        sqlite3_close(g_state->database);
        __state_destroy(g_state);
        g_state = NULL;
        return -1;
    }

    if (__load_applications_from_db(g_state) != 0 ||
        __load_transactions_from_db(g_state) != 0 ||
        __load_transaction_states_from_db(g_state) != 0 ||
        __load_transaction_logs_from_db(g_state) != 0 ||
        __initialize_transaction_id_counter(g_state) != 0) {
        sqlite3_close(g_state->database);
        __state_destroy(g_state);
        g_state = NULL;
        return -1;
    }
    
    return 0;
}

void served_state_close(void)
{
    if (g_state == NULL) {
        return;
    }

    if (g_state->database) {
        sqlite3_close(g_state->database);
    }
    
    __state_destroy(g_state);
    g_state = NULL;
}

// Execute a single deferred operation directly on the database
static int __execute_add_application_op(struct __state* state, struct state_application* application)
{
    // This is the actual database insertion logic
    const char* insert_app_query = 
        "INSERT INTO applications (name) "
        "VALUES (?)";

    sqlite3_stmt* stmt;
    int status = sqlite3_prepare_v2(state->database, insert_app_query, -1, &stmt, NULL);
    if (status != SQLITE_OK) {
        VLOG_ERROR("served", "__execute_add_application_op: failed to prepare statement: %s\n", sqlite3_errmsg(state->database));
        return -1;
    }

    sqlite3_bind_text(stmt, 1, application->name, -1, SQLITE_STATIC);

    status = sqlite3_step(stmt);
    if (status != SQLITE_DONE) {
        VLOG_ERROR("served", "__execute_add_application_op: failed to insert application: %s\n", sqlite3_errmsg(state->database));
        sqlite3_finalize(stmt);
        return -1;
    }

    int app_id = (int)sqlite3_last_insert_rowid(state->database);
    sqlite3_finalize(stmt);

    // Insert revision entry for the application (if revisions exist)
    if (application->revisions_count > 0 && application->revisions != NULL) {
        const char* insert_rev_query = 
            "INSERT INTO revisions (application_id, channel, major, minor, patch, revision, tag, size, created) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)";
            
        status = sqlite3_prepare_v2(state->database, insert_rev_query, -1, &stmt, NULL);
        if (status != SQLITE_OK) {
            VLOG_ERROR("served", "__execute_add_application_op: failed to prepare revision statement: %s\n", sqlite3_errmsg(state->database));
            return -1;
        }

        sqlite3_bind_int(stmt, 1, app_id);
        sqlite3_bind_text(stmt, 2, application->revisions[0].tracking_channel, -1, SQLITE_STATIC);
        
        if (application->revisions[0].version != NULL) {
            sqlite3_bind_int(stmt, 3, application->revisions[0].version->major);
            sqlite3_bind_int(stmt, 4, application->revisions[0].version->minor);
            sqlite3_bind_int(stmt, 5, application->revisions[0].version->patch);
            sqlite3_bind_int(stmt, 6, application->revisions[0].version->revision);
            sqlite3_bind_text(stmt, 7, application->revisions[0].version->tag, -1, SQLITE_STATIC);
            sqlite3_bind_int64(stmt, 8, application->revisions[0].version->size);
            sqlite3_bind_text(stmt, 9, application->revisions[0].version->created, -1, SQLITE_STATIC);
        } else {
            sqlite3_bind_null(stmt, 3);
            sqlite3_bind_null(stmt, 4);
            sqlite3_bind_null(stmt, 5);
            sqlite3_bind_null(stmt, 6);
            sqlite3_bind_null(stmt, 7);
            sqlite3_bind_null(stmt, 8);
            sqlite3_bind_null(stmt, 9);
        }

        status = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        
        if (status != SQLITE_DONE) {
            VLOG_ERROR("served", "__execute_add_application_op: failed to insert revision: %s\n", sqlite3_errmsg(state->database));
            return -1;
        }
    }

    // Insert commands if any
    for (int i = 0; i < application->commands_count; i++) {
        const char* insert_cmd_query = 
            "INSERT INTO commands (application_id, name, path, arguments, type) "
            "VALUES (?, ?, ?, ?, ?)";

        status = sqlite3_prepare_v2(state->database, insert_cmd_query, -1, &stmt, NULL);
        if (status != SQLITE_OK) {
            VLOG_ERROR("served", "__execute_add_application_op: failed to prepare command statement: %s\n", sqlite3_errmsg(state->database));
            return -1;
        }

        sqlite3_bind_int(stmt, 1, app_id);
        sqlite3_bind_text(stmt, 2, application->commands[i].name, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, application->commands[i].path, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 4, application->commands[i].arguments, -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 5, application->commands[i].type);

        status = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        
        if (status != SQLITE_DONE) {
            VLOG_ERROR("served", "__execute_add_application_op: failed to insert command: %s\n", sqlite3_errmsg(state->database));
            return -1;
        }
    }

    return 0;
}

static int __execute_remove_application_op(struct __state* state, const char* application_name)
{
    const char* delete_query = "DELETE FROM applications WHERE name = ?";
    sqlite3_stmt* stmt;
    int status = sqlite3_prepare_v2(state->database, delete_query, -1, &stmt, NULL);
    if (status != SQLITE_OK) {
        VLOG_ERROR("served", "__execute_remove_application_op: failed to prepare statement: %s\n", sqlite3_errmsg(state->database));
        return -1;
    }

    sqlite3_bind_text(stmt, 1, application_name, -1, SQLITE_STATIC);
    status = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (status != SQLITE_DONE) {
        VLOG_ERROR("served", "__execute_remove_application_op: failed to delete application: %s\n", sqlite3_errmsg(state->database));
        return -1;
    }

    return 0;
}

static int __execute_add_transaction_op(struct __state* state, struct served_transaction* transaction)
{
    const char* insert_tx_query = 
        "INSERT INTO transactions (id, type, state, flags, name, description, wait_type, wait_data) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?)";

    sqlite3_stmt* stmt;
    int status = sqlite3_prepare_v2(state->database, insert_tx_query, -1, &stmt, NULL);
    if (status != SQLITE_OK) {
        VLOG_ERROR("served", "__execute_add_transaction_op: failed to prepare statement: %s\n", sqlite3_errmsg(state->database));
        return -1;
    }

    sqlite3_bind_int(stmt, 1, transaction->id);
    sqlite3_bind_int(stmt, 2, transaction->type);
    sqlite3_bind_int(stmt, 3, served_sm_current_state(&transaction->sm));
    sqlite3_bind_int(stmt, 4, 0);  // flags - placeholder
    sqlite3_bind_text(stmt, 5, transaction->name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 6, transaction->description, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 7, transaction->wait.type);
    sqlite3_bind_int(stmt, 8, transaction->wait.data.transaction_id);

    status = sqlite3_step(stmt);
    if (status != SQLITE_DONE) {
        VLOG_ERROR("served", "__execute_add_transaction_op: failed to insert transaction: %s\n", sqlite3_errmsg(state->database));
        sqlite3_finalize(stmt);
        return -1;
    }

    sqlite3_finalize(stmt);
    return 0;
}

static int __execute_update_transaction_op(struct __state* state, struct served_transaction* transaction)
{
    const char* update_tx_query = 
        "UPDATE transactions SET type = ?, state = ?, flags = ?, name = ?, description = ?, "
        "wait_type = ?, wait_data = ? WHERE id = ?";

    sqlite3_stmt* stmt;
    int status = sqlite3_prepare_v2(state->database, update_tx_query, -1, &stmt, NULL);
    if (status != SQLITE_OK) {
        VLOG_ERROR("served", "__execute_update_transaction_op: failed to prepare statement: %s\n", sqlite3_errmsg(state->database));
        return -1;
    }

    sqlite3_bind_int(stmt, 1, transaction->type);
    sqlite3_bind_int(stmt, 2, served_sm_current_state(&transaction->sm));
    sqlite3_bind_int(stmt, 3, 0);  // flags - placeholder
    sqlite3_bind_text(stmt, 4, transaction->name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 5, transaction->description, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 6, transaction->wait.type);
    sqlite3_bind_int(stmt, 7, transaction->wait.data.transaction_id);
    sqlite3_bind_int(stmt, 8, transaction->id);

    status = sqlite3_step(stmt);
    if (status != SQLITE_DONE) {
        VLOG_ERROR("served", "__execute_update_transaction_op: failed to update transaction: %s\n", sqlite3_errmsg(state->database));
        sqlite3_finalize(stmt);
        return -1;
    }

    sqlite3_finalize(stmt);
    return 0;
}

static int __execute_update_tx_state_op(struct __state* state, struct state_transaction* transaction)
{
    const char* update_tx_state_query = 
        "UPDATE transactions_state SET name = ?, channel = ?, revision = ? "
        "WHERE transaction_id = ?";

    sqlite3_stmt* stmt;
    int status = sqlite3_prepare_v2(state->database, update_tx_state_query, -1, &stmt, NULL);
    if (status != SQLITE_OK) {
        VLOG_ERROR("served", "__execute_update_tx_state_op: failed to prepare statement: %s\n", sqlite3_errmsg(state->database));
        return -1;
    }

    sqlite3_bind_text(stmt, 1, transaction->name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, transaction->channel, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 3, transaction->revision);
    sqlite3_bind_int(stmt, 4, transaction->id);

    status = sqlite3_step(stmt);
    if (status != SQLITE_DONE) {
        VLOG_ERROR("served", "__execute_update_tx_state_op: failed to update transaction state: %s\n", sqlite3_errmsg(state->database));
        sqlite3_finalize(stmt);
        return -1;
    }

    sqlite3_finalize(stmt);
    return 0;
}

static int __execute_complete_tx_op(struct __state* state, unsigned int transaction_id)
{
    const char* complete_tx_query = "UPDATE transactions SET completed_at = strftime('%s', 'now') WHERE id = ?";
    sqlite3_stmt* stmt;
    int status = sqlite3_prepare_v2(state->database, complete_tx_query, -1, &stmt, NULL);
    if (status != SQLITE_OK) {
        VLOG_ERROR("served", "__execute_complete_tx_op: failed to prepare statement: %s\n", sqlite3_errmsg(state->database));
        return -1;
    }

    sqlite3_bind_int(stmt, 1, transaction_id);
    status = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (status != SQLITE_DONE) {
        VLOG_ERROR("served", "__execute_complete_tx_op: failed to mark transaction complete: %s\n", sqlite3_errmsg(state->database));
        return -1;
    }

    VLOG_DEBUG("served", "__execute_complete_tx_op: marked transaction %u as completed\n", transaction_id);
    return 0;
}

static int __execute_add_transaction_log_op(struct __state* state, void* log_data)
{
    struct {
        unsigned int                      transaction_id;
        enum served_transaction_log_level level;
        time_t                            timestamp;
        sm_state_t                        txstate;
        char*                             message;
    }* data = log_data;

    const char* insert_log_query = 
        "INSERT INTO transaction_logs (transaction_id, level, timestamp, state, message) "
        "VALUES (?, ?, ?, ?, ?)";

    sqlite3_stmt* stmt;
    int status = sqlite3_prepare_v2(state->database, insert_log_query, -1, &stmt, NULL);
    if (status != SQLITE_OK) {
        VLOG_ERROR("served", "__execute_add_transaction_log_op: failed to prepare statement: %s\n", sqlite3_errmsg(state->database));
        return -1;
    }

    sqlite3_bind_int(stmt, 1, data->transaction_id);
    sqlite3_bind_int(stmt, 2, data->level);
    sqlite3_bind_int64(stmt, 3, (sqlite3_int64)data->timestamp);
    sqlite3_bind_int(stmt, 4, data->txstate);
    sqlite3_bind_text(stmt, 5, data->message, -1, SQLITE_STATIC);

    status = sqlite3_step(stmt);
    if (status != SQLITE_DONE) {
        VLOG_ERROR("served", "__execute_add_transaction_log_op: failed to insert log: %s\n", sqlite3_errmsg(state->database));
        sqlite3_finalize(stmt);
        return -1;
    }

    sqlite3_finalize(stmt);
    return 0;
}

static int __execute_add_tx_state_op(struct __state* state, unsigned int transactionID, struct state_transaction* transaction)
{
    const char* insertTxStateSQL = 
        "INSERT INTO transactions_state (transaction_id, name, channel, revision) "
        "VALUES (?, ?, ?, ?)";

    sqlite3_stmt* stmt;
    int           status;

    status = sqlite3_prepare_v2(state->database, insertTxStateSQL, -1, &stmt, NULL);
    if (status != SQLITE_OK) {
        VLOG_ERROR("served", "__execute_add_tx_state_op: failed to prepare state statement: %s\n", sqlite3_errmsg(state->database));
        return -1;
    }

    sqlite3_bind_int(stmt, 1, transactionID);
    sqlite3_bind_text(stmt, 2, transaction->name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, transaction->channel, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 4, transaction->revision);

    status = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (status != SQLITE_DONE) {
        VLOG_ERROR("served", "__execute_add_tx_state_op: failed to insert transaction state: %s\n", sqlite3_errmsg(state->database));
        return -1;
    }

    return 0;
}

// Execute all deferred operations in a single transaction
static int __execute_deferred_operations(struct __state* state)
{
    struct list_item* item;
    if (state->deferred_ops.count == 0) {
        return 0; // Nothing to do
    }

    VLOG_DEBUG("served", "__execute_deferred_operations: executing deferred operations\n");

    char* errMsg = NULL;
    int status = sqlite3_exec(state->database, "BEGIN TRANSACTION", NULL, NULL, &errMsg);
    if (status != SQLITE_OK) {
        VLOG_ERROR("served", "__execute_deferred_operations: failed to begin transaction: %s\n", errMsg);
        sqlite3_free(errMsg);
        return -1;
    }

    list_foreach(&state->deferred_ops, item) {
        struct deferred_operation* op = (struct deferred_operation*)item;
        int result = 0;
        
        switch (op->type) {
            case DEFERRED_OP_ADD_APPLICATION:
                result = __execute_add_application_op(state, op->data.add_app.application);
                break;
                
            case DEFERRED_OP_REMOVE_APPLICATION:
                result = __execute_remove_application_op(state, op->data.remove_app.application_name);
                break;
                
            case DEFERRED_OP_ADD_TRANSACTION:
                result = __execute_add_transaction_op(state, op->data.add_tx.transaction);
                break;
                
            case DEFERRED_OP_UPDATE_TRANSACTION:
                result = __execute_update_transaction_op(state, op->data.update_tx.transaction);
                break;
                
            case DEFERRED_OP_ADD_TRANSACTION_STATE:
                result = __execute_add_tx_state_op(
                    state,
                    op->data.add_tx_state.transaction_id,
                    op->data.add_tx_state.transaction
                );
                break;

            case DEFERRED_OP_UPDATE_TRANSACTION_STATE:
                result = __execute_update_tx_state_op(state, op->data.update_tx_state.transaction);
                break;

            case DEFERRED_OP_COMPLETE_TRANSACTION:
                result = __execute_complete_tx_op(state, op->data.complete_tx.transaction_id);
                break;

            case DEFERRED_OP_ADD_TRANSACTION_LOG:
                result = __execute_add_transaction_log_op(state, &op->data.add_log);
                free(op->data.add_log.message);
                break;
        }
        
        if (result != 0) {
            VLOG_ERROR("served", "__execute_deferred_operations: operation failed, rolling back\n");
            sqlite3_exec(state->database, "ROLLBACK", NULL, NULL, NULL);
            return -1;
        }
    }

    status = sqlite3_exec(state->database, "COMMIT", NULL, NULL, &errMsg);
    if (status != SQLITE_OK) {
        VLOG_ERROR("served", "__execute_deferred_operations: failed to commit transaction: %s\n", errMsg);
        sqlite3_free(errMsg);
        sqlite3_exec(state->database, "ROLLBACK", NULL, NULL, NULL);
        return -1;
    }

    __clear_deferred_operations(state);
    VLOG_DEBUG("served", "__execute_deferred_operations: all operations committed successfully\n");
    return 0;
}

void served_state_lock(void)
{
    mtx_lock(&g_state->lock);
    g_state->lock_count++;
}

void served_state_unlock(void)
{
    // Execute all deferred operations before unlocking
    if (g_state->deferred_ops.count > 0 && g_state->lock_count == 1) {
        if (__execute_deferred_operations(g_state) != 0) {
            VLOG_ERROR("served", "served_state_unlock: failed to execute deferred operations\n");
            // Note: in-memory state may be inconsistent with database now
            // You may want to reload state from database here
        }
        
        // Save state to database here if needed
        if (served_state_flush() != 0) {
            VLOG_ERROR("served", "served_state_unlock: failed to flush dirty state\n");
        }
    }

    g_state->lock_count--;
    mtx_unlock(&g_state->lock);
}

int served_state_flush(void)
{
    if (g_state == NULL) {
        VLOG_ERROR("served", "served_state_flush: state not initialized\n");
        return -1;
    }

    // In our SQLite-based implementation, data is already persisted to the database
    // when applications/transactions are added/modified. This function ensures 
    // any pending changes are committed and synced.
    
    // Flush any pending database operations
    int status = sqlite3_exec(g_state->database, "PRAGMA synchronous = FULL", NULL, NULL, NULL);
    if (status != SQLITE_OK) {
        VLOG_ERROR("served", "served_state_flush: failed to sync database: %s\n", sqlite3_errmsg(g_state->database));
        return -1;
    }

    VLOG_DEBUG("served", "served_state_flush: state saved successfully\n");
    return 0;
}

// This must be called with the state lock held
struct state_transaction* served_state_transaction(unsigned int id)
{
    if (g_state == NULL) {
        return NULL;
    }

    if (g_state->lock_count == 0) {
        VLOG_ERROR("served", "served_state_transaction: state lock not held\n");
        return NULL;
    }

    for (int i = 0; i < g_state->transaction_state_count; i++) {
        if (g_state->transaction_states[i].id == id) {
            return &g_state->transaction_states[i];
        }
    }

    return NULL;
}

// This must be called with the state lock held
struct state_application* served_state_application(const char* name)
{
    if (g_state == NULL || name == NULL) {
        return NULL;
    }

    if (g_state->lock_count == 0) {
        VLOG_ERROR("served", "served_state_application: state lock not held\n");
        return NULL;
    }

    for (int i = 0; i < g_state->applications_states_count; i++) {
        if (g_state->applications_states[i].name && 
            strcmp(g_state->applications_states[i].name, name) == 0) {
            return &g_state->applications_states[i];
        }
    }

    return NULL;
}

// This must be called with the state lock held
int served_state_add_application(struct state_application* application)
{
    struct deferred_operation* op;
    struct state_application*  newApps;
    
    if (g_state == NULL || application == NULL || application->name == NULL) {
        return -1;
    }

    if (g_state->lock_count == 0) {
        VLOG_ERROR("served", "served_state_add_application: state lock not held\n");
        return -1;
    }

    // Check if application already exists
    if (served_state_application(application->name) != NULL) {
        VLOG_ERROR("served", "served_state_add_application: application '%s' already exists\n", application->name);
        return -1;
    }

    // Add to in-memory state immediately for read consistency
    newApps = realloc(g_state->applications_states,
        sizeof(struct state_application) * (g_state->applications_states_count + 1));
    if (!newApps) {
        VLOG_ERROR("served", "served_state_add_application: failed to allocate memory\n");
        return -1;
    }
    
    g_state->applications_states = newApps;
    g_state->applications_states[g_state->applications_states_count] = *application;
    g_state->applications_states_count++;

    op = malloc(sizeof(struct deferred_operation));
    if (!op) {
        VLOG_ERROR("served", "served_state_add_application: failed to allocate deferred operation\n");
        // Roll back in-memory change
        g_state->applications_states_count--;
        return -1;
    }
    
    op->type = DEFERRED_OP_ADD_APPLICATION;
    op->data.add_app.application = &g_state->applications_states[g_state->applications_states_count - 1];
    
    __enqueue_deferred_operation(g_state, op);
    
    VLOG_DEBUG("served", "served_state_add_application: operation deferred for '%s'\n", application->name);
    return 0;
}

// This must be called with the state lock held
int served_state_remove_application(struct state_application* application)
{
    struct deferred_operation* op;
    int                        found = -1;

    if (g_state == NULL || application == NULL || application->name == NULL) {
        return -1;
    }

    if (g_state->lock_count == 0) {
        VLOG_ERROR("served", "served_state_remove_application: state lock not held\n");
        return -1;
    }

    // Find and remove from in-memory state first
    for (int i = 0; i < g_state->applications_states_count; i++) {
        if (g_state->applications_states[i].name && 
            strcmp(g_state->applications_states[i].name, application->name) == 0) {
            found = i;
            break;
        }
    }

    if (found == -1) {
        VLOG_ERROR("served", "served_state_remove_application: application '%s' not found\n", application->name);
        return -1;
    }

    // Remove from in-memory state
    __state_application_delete(&g_state->applications_states[found]);
    
    // Shift remaining applications
    for (int j = found; j < g_state->applications_states_count - 1; j++) {
        g_state->applications_states[j] = g_state->applications_states[j + 1];
    }
    g_state->applications_states_count--;

    op = __deferred_operation_new(DEFERRED_OP_REMOVE_APPLICATION);
    if (op == NULL) {
        VLOG_ERROR("served", "served_state_remove_application: failed to allocate deferred operation\n");
        return -1;
    }
    
    op->data.remove_app.application_name = platform_strdup(application->name);
    if (!op->data.remove_app.application_name) {
        free(op);
        return -1;
    }
    
    __enqueue_deferred_operation(g_state, op);
    
    VLOG_DEBUG("served", "served_state_remove_application: operation deferred for '%s'\n", application->name);
    return 0;
}

// This must be called with the state lock held
unsigned int served_state_transaction_new(struct served_transaction_options* options)
{
    struct deferred_operation* op;
    struct served_transaction* tx;
    struct served_transaction* newTxs;
    unsigned int               transactionID;
    
    if (g_state == NULL || options == NULL) {
        return 0;
    }

    if (g_state->lock_count == 0) {
        VLOG_ERROR("served", "served_state_transaction_new: state lock not held\n");
        return 0;
    }

    // Generate the transaction ID now
    transactionID = g_state->next_transaction_id++;

    // Add to in-memory state immediately for read consistency
    newTxs = realloc(g_state->transactions,
        sizeof(struct served_transaction) * (g_state->transactions_count + 1));
    if (newTxs == NULL) {
        VLOG_ERROR("served", "served_state_transaction_new: failed to allocate memory\n");
        // Roll back ID counter
        g_state->next_transaction_id--;
        return 0;
    }
    
    g_state->transactions = newTxs;
    
    // Initialize the new transaction with the real ID
    tx = &g_state->transactions[g_state->transactions_count];
    struct served_transaction_options opts = *options;
    opts.id = transactionID;  // Set the generated ID
    served_transaction_construct(tx, &opts);
    g_state->transactions_count++;

    // Defer the database operation
    op = __deferred_operation_new(DEFERRED_OP_ADD_TRANSACTION);
    if (op == NULL) {
        VLOG_ERROR("served", "served_state_transaction_new: failed to allocate deferred operation\n");
        // Roll back in-memory change and clean up allocated memory
        g_state->transactions_count--;
        __served_transaction_delete(tx);
        g_state->next_transaction_id--;
        return 0;
    }
    
    op->data.add_tx.transaction = tx;
    
    __enqueue_deferred_operation(g_state, op);
    
    VLOG_DEBUG("served", "served_state_transaction_new: operation deferred for transaction %u\n", transactionID);
    return transactionID;
}

// This must be called with the state lock held
int served_state_transaction_update(struct served_transaction* transaction)
{
    struct deferred_operation* op;

    if (g_state == NULL || transaction == NULL) {
        return -1;
    }

    if (g_state->lock_count == 0) {
        VLOG_ERROR("served", "served_state_transaction_update: state lock not held\n");
        return -1;
    }

    // The in-memory transaction is already updated by the caller
    // We just need to defer the database write
    op = __deferred_operation_new(DEFERRED_OP_UPDATE_TRANSACTION);
    if (op == NULL) {
        VLOG_ERROR("served", "served_state_transaction_update: failed to allocate deferred operation\n");
        return -1;
    }

    op->data.update_tx.transaction = transaction;
    
    __enqueue_deferred_operation(g_state, op);
    return 0;
}

int served_state_transaction_log_add(
    unsigned int transaction_id,
    enum served_transaction_log_level level,
    time_t timestamp,
    sm_state_t state,
    const char* message)
{
    struct deferred_operation* op;
    struct state_transaction*  state_tx = NULL;

    if (g_state == NULL || message == NULL) {
        return -1;
    }

    if (g_state->lock_count == 0) {
        VLOG_ERROR("served", "served_state_transaction_log_add: state lock not held\n");
        return -1;
    }

    // Find the state_transaction for this transaction_id
    for (int i = 0; i < g_state->transaction_state_count; i++) {
        if (g_state->transaction_states[i].id == transaction_id) {
            state_tx = &g_state->transaction_states[i];
            break;
        }
    }

    if (state_tx == NULL) {
        VLOG_WARNING("served", "served_state_transaction_log_add: no state for transaction %u\n", transaction_id);
        return -1;
    }

    // Add to in-memory state immediately
    struct state_transaction_log* new_logs = realloc(state_tx->logs, 
        sizeof(struct state_transaction_log) * (state_tx->logs_count + 1));
    if (new_logs == NULL) {
        VLOG_ERROR("served", "served_state_transaction_log_add: failed to allocate log entry\n");
        return -1;
    }

    state_tx->logs = new_logs;
    struct state_transaction_log* log = &state_tx->logs[state_tx->logs_count++];
    log->level = level;
    log->timestamp = timestamp;
    log->state = state;
    strncpy(log->message, message, sizeof(log->message) - 1);
    log->message[sizeof(log->message) - 1] = '\0';

    // Defer database operation
    op = __deferred_operation_new(DEFERRED_OP_ADD_TRANSACTION_LOG);
    if (op == NULL) {
        VLOG_ERROR("served", "served_state_transaction_log_add: failed to allocate deferred operation\n");
        // Roll back in-memory change
        state_tx->logs_count--;
        return -1;
    }

    op->data.add_log.transaction_id = transaction_id;
    op->data.add_log.level = level;
    op->data.add_log.timestamp = timestamp;
    op->data.add_log.state = state;
    op->data.add_log.message = platform_strdup(message);

    __enqueue_deferred_operation(g_state, op);
    return 0;
}

int served_state_transaction_logs(
    unsigned int transaction_id,
    struct state_transaction_log** logs_out,
    int* count_out)
{
    if (g_state == NULL || logs_out == NULL || count_out == NULL) {
        return -1;
    }

    if (g_state->lock_count == 0) {
        VLOG_ERROR("served", "served_state_transaction_logs: state lock not held\n");
        return -1;
    }

    // Find the state_transaction for this transaction_id
    for (int i = 0; i < g_state->transaction_state_count; i++) {
        if (g_state->transaction_states[i].id == transaction_id) {
            *logs_out = g_state->transaction_states[i].logs;
            *count_out = g_state->transaction_states[i].logs_count;
            return 0;
        }
    }

    *logs_out = NULL;
    *count_out = 0;
    return -1;
}

// This must be called with the state lock held
int served_state_transaction_state_new(unsigned int id, struct state_transaction* state)
{
    struct deferred_operation* op;
    struct state_transaction*  newStates;

    if (g_state == NULL || state == NULL) {
        return -1;
    }

    if (g_state->lock_count == 0) {
        VLOG_ERROR("served", "served_state_transaction_state_new: state lock not held\n");
        return -1;
    }

    // Add to in-memory state immediately for read consistency
    newStates = realloc(g_state->transaction_states, 
        sizeof(struct state_transaction) * (g_state->transaction_state_count + 1));
    if (!newStates) {
        VLOG_ERROR("served", "served_state_transaction_state_new: failed to allocate memory\n");
        return -1;
    }
    
    g_state->transaction_states = newStates;
    g_state->transaction_states[g_state->transaction_state_count] = *state;
    g_state->transaction_states[g_state->transaction_state_count].id = id;
    g_state->transaction_states[g_state->transaction_state_count].logs = NULL;
    g_state->transaction_states[g_state->transaction_state_count].logs_count = 0;
    g_state->transaction_state_count++;

    // Defer the database operation
    op = __deferred_operation_new(DEFERRED_OP_ADD_TRANSACTION_STATE);
    if (op == NULL) {
        VLOG_ERROR("served", "served_state_transaction_state_new: failed to allocate deferred operation\n");
        // Roll back in-memory change
        g_state->transaction_state_count--;
        return -1;
    }

    op->data.add_tx_state.transaction_id = id;
    op->data.add_tx_state.transaction = &g_state->transaction_states[g_state->transaction_state_count - 1];

    __enqueue_deferred_operation(g_state, op);
    return 0;
}

// This must be called with the state lock held
int served_state_transaction_state_update(struct state_transaction* state)
{
    struct deferred_operation* op;

    if (g_state == NULL || state == NULL) {
        return -1;
    }

    if (g_state->lock_count == 0) {
        VLOG_ERROR("served", "served_state_transaction_state_update: state lock not held\n");
        return -1;
    }

    // The in-memory transaction state is already updated by the caller
    // We just need to defer the database write
    op = __deferred_operation_new(DEFERRED_OP_UPDATE_TRANSACTION_STATE);
    if (op == NULL) {
        VLOG_ERROR("served", "served_state_transaction_state_update: failed to allocate deferred operation\n");
        return -1;
    }

    op->data.update_tx_state.transaction = state;
    
    __enqueue_deferred_operation(g_state, op);
    return 0;
}

// This must be called with the state lock held
int served_state_get_applications(struct state_application** applicationsOut, int* applicationsCount)
{
    if (g_state == NULL || applicationsOut == NULL || applicationsCount == NULL) {
        return -1;
    }

    if (g_state->lock_count == 0) {
        VLOG_ERROR("served", "served_state_get_applications: state lock not held\n");
        return -1;
    }

    *applicationsOut = g_state->applications_states;
    *applicationsCount = g_state->applications_states_count;
    return 0;
}

// This must be called with the state lock held
int served_state_get_transactions(struct served_transaction** transactionsOut, int* transactionsCount)
{
    if (g_state == NULL || transactionsOut == NULL || transactionsCount == NULL) {
        return -1;
    }

    if (g_state->lock_count == 0) {
        VLOG_ERROR("served", "served_state_get_transactions: state lock not held\n");
        return -1;
    }

    *transactionsOut = g_state->transactions;
    *transactionsCount = g_state->transactions_count;
    return 0;
}

// This must be called with the state lock held
int served_state_get_transaction_states(struct state_transaction** transactionsOut, int* transactionsCount)
{
    if (g_state == NULL || transactionsOut == NULL || transactionsCount == NULL) {
        return -1;
    }

    if (g_state->lock_count == 0) {
        VLOG_ERROR("served", "served_state_get_transaction_states: state lock not held\n");
        return -1;
    }

    *transactionsOut = g_state->transaction_states;
    *transactionsCount = g_state->transaction_state_count;
    return 0;
}

int served_state_transaction_complete(unsigned int id)
{
    struct deferred_operation* op;

    if (g_state == NULL) {
        VLOG_ERROR("served", "served_state_transaction_complete: state not initialized\n");
        return -1;
    }

    if (g_state->lock_count == 0) {
        VLOG_ERROR("served", "served_state_transaction_complete: state lock not held\n");
        return -1;
    }

    op = __deferred_operation_new(DEFERRED_OP_COMPLETE_TRANSACTION);
    if (op == NULL) {
        return -1;
    }

    op->data.complete_tx.transaction_id = id;
    __enqueue_deferred_operation(g_state, op);
    
    VLOG_DEBUG("served", "served_state_transaction_complete: deferred completion of transaction %u\n", id);
    return 0;
}

int served_state_transaction_cleanup(void)
{
    const char* cleanup_query =
        "DELETE FROM transactions WHERE id IN ("
        "  SELECT id FROM ("
        "    SELECT id, completed_at, "
        "           ROW_NUMBER() OVER (ORDER BY completed_at DESC) as rn "
        "    FROM transactions "
        "    WHERE completed_at IS NOT NULL "
        "    ORDER BY completed_at DESC"
        "  ) "
        "  WHERE rn > 10 AND completed_at < strftime('%s', 'now', '-7 days')"
        ")";
    
    sqlite3_stmt* stmt;
    int status;
    int changes;

    if (g_state == NULL) {
        VLOG_ERROR("served", "served_state_transaction_cleanup: state not initialized\n");
        return -1;
    }

    if (g_state->lock_count == 0) {
        VLOG_ERROR("served", "served_state_transaction_cleanup: state lock not held\n");
        return -1;
    }

    status = sqlite3_prepare_v2(g_state->database, cleanup_query, -1, &stmt, NULL);
    if (status != SQLITE_OK) {
        VLOG_ERROR("served", "served_state_transaction_cleanup: failed to prepare statement: %s\n", 
                   sqlite3_errmsg(g_state->database));
        return -1;
    }

    status = sqlite3_step(stmt);
    changes = sqlite3_changes(g_state->database);
    sqlite3_finalize(stmt);

    if (status != SQLITE_DONE) {
        VLOG_ERROR("served", "served_state_transaction_cleanup: failed to cleanup transactions: %s\n", 
                   sqlite3_errmsg(g_state->database));
        return -1;
    }

    VLOG_DEBUG("served", "served_state_transaction_cleanup: deleted %d old transactions\n", changes);
    return changes;
}
