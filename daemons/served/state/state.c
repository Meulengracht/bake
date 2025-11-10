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
// - id (INTEGER PRIMARY KEY AUTOINCREMENT)
// - type (INTEGER)
// - state (INTEGER)
// - flags (INTEGER)
// - name (TEXT)
// - channel (TEXT)
// - revision (INTEGER)
// 
static const char* g_transactionsTableSQL = 
    "CREATE TABLE IF NOT EXISTS transactions ("
    "id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "type INTEGER NOT NULL,"
    "flags INTEGER NOT NULL,"
    "state INTEGER NOT NULL,"
    ");";

static const char* g_transactionsStateTableSQL = 
    "CREATE TABLE IF NOT EXISTS transactions_state ("
    "id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "transaction_id INTEGER NOT NULL,"
    "name TEXT,"
    "channel TEXT,"
    "revision INTEGER,"
    "FOREIGN KEY(transaction_id) REFERENCES transactions(id) ON DELETE CASCADE"
    ");";

// The state implementation will provide functions to add, remove, and query applications and transactions.
// The state also will allow for transactional changes to ensure consistency across multiple operations, and
// to ensure resilience against crashes and restarts

#include <errno.h>
#include <linux/limits.h>
#include <chef/platform.h>
#include <chef/package.h>
#include <sqlite3.h>
#include <state.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <utils.h>
#include <vlog.h>

struct __state {
    struct state_transaction* transaction_states;
    int                       transaction_state_count;
    struct state_application* applications_states;
    int                       applications_states_count;

    sqlite3* database;
    mtx_t    lock;
    bool     is_dirty;
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

static void __state_destroy(struct __state* state)
{
    if (state == NULL) {
        return;
    }

    for (int i = 0; i < state->applications_states_count; i++) {
        __state_application_delete(&state->applications_states[i]);
    }
    free((void*)state->applications_states);

    for (int i = 0; i < state->transaction_state_count; i++) {
        __state_transaction_delete(&state->transaction_states[i]);
    }
    free((void*)state->transaction_states);
    mtx_destroy(&state->lock);
    free((void*)state);
}


static const char* __get_state_path(void)
{
    return served_paths_path("/var/chef/state.db");
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
        application->name = strdup(name);
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
        application->revisions[i].tracking_channel = channel ? strdup(channel) : strdup("stable");

        // Allocate and populate version structure
        application->revisions[i].version = malloc(sizeof(struct chef_version));
        if (application->revisions[i].version) {
            memset(application->revisions[i].version, 0, sizeof(struct chef_version));
            application->revisions[i].version->major = major;
            application->revisions[i].version->minor = minor;
            application->revisions[i].version->patch = patch;
            application->revisions[i].version->revision = revision;
            application->revisions[i].version->tag = tag ? strdup(tag) : NULL;
            application->revisions[i].version->size = size;
            application->revisions[i].version->created = created ? strdup(created) : NULL;
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
        state->transaction_states[i].name = name ? strdup(name) : NULL;
        state->transaction_states[i].channel = channel ? strdup(channel) : NULL;
        state->transaction_states[i].revision = revision;

        i++;
    }

    sqlite3_finalize(stmt);
    VLOG_DEBUG("served", "__load_transaction_states_from_db: loaded %d transactions\n", transaction_count);
    return 0;
}

int served_state_load(void)
{
    int         status;
    const char* path = __get_state_path();
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
        __load_transaction_states_from_db(g_state) != 0) {
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

void served_state_lock(void)
{
    mtx_lock(&g_state->lock);
}

void served_state_unlock(void)
{
    if (g_state->is_dirty) {
        // Save state to database here if needed
        if (served_state_flush() == 0) {
            g_state->is_dirty = false;
        } else {
            VLOG_ERROR("served", "served_state_unlock: failed to flush dirty state\n");
        }
    }
    mtx_unlock(&g_state->lock);
}

void served_state_mark_dirty(void)
{
    g_state->is_dirty = true;
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

    g_state->is_dirty = false;
    VLOG_DEBUG("served", "served_state_flush: state saved successfully\n");
    return 0;
}

unsigned int served_state_transaction_new(struct state_transaction* transaction)
{
    if (g_state == NULL) {
        return -1;
    }

    const char* insert_query = 
        "INSERT INTO transactions (name, channel, revision) "
        "VALUES (?, ?, ?)";

    sqlite3_stmt* stmt;
    int status = sqlite3_prepare_v2(g_state->database, insert_query, -1, &stmt, NULL);
    if (status != SQLITE_OK) {
        VLOG_ERROR("served", "served_state_transaction_new: failed to prepare statement: %s\n", sqlite3_errmsg(g_state->database));
        return 0;
    }

    sqlite3_bind_text(stmt, 1, transaction->name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, transaction->channel, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 3, transaction->revision);

    status = sqlite3_step(stmt);
    if (status != SQLITE_DONE) {
        VLOG_ERROR("served", "served_state_transaction_new: failed to insert transaction: %s\n", sqlite3_errmsg(g_state->database));
        sqlite3_finalize(stmt);
        return 0;
    }

    unsigned int transaction_id = (unsigned int)sqlite3_last_insert_rowid(g_state->database);
    sqlite3_finalize(stmt);

    // Add to in-memory state
    struct state_transaction* new_states = realloc(g_state->transaction_states, 
        sizeof(struct state_transaction) * (g_state->transaction_state_count + 1));
    if (new_states) {
        g_state->transaction_states = new_states;
        g_state->transaction_states[g_state->transaction_state_count] = *transaction;
        g_state->transaction_states[g_state->transaction_state_count].id = transaction_id;
        g_state->transaction_state_count++;
        served_state_mark_dirty();
    }

    return transaction_id;
}

struct state_transaction* served_state_transaction(unsigned int id)
{
    if (g_state == NULL) {
        return NULL;
    }

    for (int i = 0; i < g_state->transaction_state_count; i++) {
        if (g_state->transaction_states[i].id == id) {
            return &g_state->transaction_states[i];
        }
    }

    return NULL;
}

struct state_application* served_state_application(const char* name)
{
    if (g_state == NULL || name == NULL) {
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

int served_state_add_application(struct state_application* application)
{
    if (g_state == NULL || application == NULL || application->name == NULL) {
        return -1;
    }

    // Check if application already exists
    if (served_state_application(application->name) != NULL) {
        VLOG_ERROR("served", "served_state_add_application: application '%s' already exists\n", application->name);
        return -1;
    }

    // Begin transaction
    char* errMsg = NULL;
    int status = sqlite3_exec(g_state->database, "BEGIN TRANSACTION", NULL, NULL, &errMsg);
    if (status != SQLITE_OK) {
        VLOG_ERROR("served", "served_state_add_application: failed to begin transaction: %s\n", errMsg);
        sqlite3_free(errMsg);
        return -1;
    }

    // Insert application into database
    const char* insert_app_query = 
        "INSERT INTO applications (name) "
        "VALUES (?)";

    sqlite3_stmt* stmt;
    status = sqlite3_prepare_v2(g_state->database, insert_app_query, -1, &stmt, NULL);
    if (status != SQLITE_OK) {
        VLOG_ERROR("served", "served_state_add_application: failed to prepare statement: %s\n", sqlite3_errmsg(g_state->database));
        sqlite3_exec(g_state->database, "ROLLBACK", NULL, NULL, NULL);
        return -1;
    }

    sqlite3_bind_text(stmt, 1, application->name, -1, SQLITE_STATIC);

    status = sqlite3_step(stmt);
    if (status != SQLITE_DONE) {
        VLOG_ERROR("served", "served_state_add_application: failed to insert application: %s\n", sqlite3_errmsg(g_state->database));
        sqlite3_finalize(stmt);
        sqlite3_exec(g_state->database, "ROLLBACK", NULL, NULL, NULL);
        return -1;
    }

    int app_id = (int)sqlite3_last_insert_rowid(g_state->database);
    sqlite3_finalize(stmt);

    // Insert revision entry for the application (if revisions exist)
    if (application->revisions_count > 0 && application->revisions != NULL) {
        const char* insert_rev_query = 
            "INSERT INTO revisions (application_id, channel, major, minor, patch, revision, tag, size, created) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)";
            
        status = sqlite3_prepare_v2(g_state->database, insert_rev_query, -1, &stmt, NULL);
        if (status != SQLITE_OK) {
            VLOG_ERROR("served", "served_state_add_application: failed to prepare revision statement: %s\n", sqlite3_errmsg(g_state->database));
            sqlite3_exec(g_state->database, "ROLLBACK", NULL, NULL, NULL);
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
            // Bind NULL values if version is NULL
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
            VLOG_ERROR("served", "served_state_add_application: failed to insert revision: %s\n", sqlite3_errmsg(g_state->database));
            sqlite3_exec(g_state->database, "ROLLBACK", NULL, NULL, NULL);
            return -1;
        }
    }

    // Insert commands if any
    for (int i = 0; i < application->commands_count; i++) {
        const char* insert_cmd_query = 
            "INSERT INTO commands (application_id, name, path, arguments, type) "
            "VALUES (?, ?, ?, ?, ?)";

        status = sqlite3_prepare_v2(g_state->database, insert_cmd_query, -1, &stmt, NULL);
        if (status != SQLITE_OK) {
            VLOG_ERROR("served", "served_state_add_application: failed to prepare command statement: %s\n", sqlite3_errmsg(g_state->database));
            sqlite3_exec(g_state->database, "ROLLBACK", NULL, NULL, NULL);
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
            VLOG_ERROR("served", "served_state_add_application: failed to insert command: %s\n", sqlite3_errmsg(g_state->database));
            sqlite3_exec(g_state->database, "ROLLBACK", NULL, NULL, NULL);
            return -1;
        }
    }

    // Commit transaction
    status = sqlite3_exec(g_state->database, "COMMIT", NULL, NULL, &errMsg);
    if (status != SQLITE_OK) {
        VLOG_ERROR("served", "served_state_add_application: failed to commit transaction: %s\n", errMsg);
        sqlite3_free(errMsg);
        sqlite3_exec(g_state->database, "ROLLBACK", NULL, NULL, NULL);
        return -1;
    }

    // Add to in-memory state
    struct state_application* new_apps = realloc(g_state->applications_states,
        sizeof(struct state_application) * (g_state->applications_states_count + 1));
    if (new_apps) {
        g_state->applications_states = new_apps;
        g_state->applications_states[g_state->applications_states_count] = *application;
        g_state->applications_states_count++;
        served_state_mark_dirty();
    }

    return 0;
}

int served_state_remove_application(struct state_application* application)
{
    if (g_state == NULL || application == NULL || application->name == NULL) {
        return -1;
    }

    // Remove from database
    const char* delete_query = "DELETE FROM applications WHERE name = ?";
    sqlite3_stmt* stmt;
    int status = sqlite3_prepare_v2(g_state->database, delete_query, -1, &stmt, NULL);
    if (status != SQLITE_OK) {
        VLOG_ERROR("served", "served_state_remove_application: failed to prepare statement: %s\n", sqlite3_errmsg(g_state->database));
        return -1;
    }

    sqlite3_bind_text(stmt, 1, application->name, -1, SQLITE_STATIC);
    status = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (status != SQLITE_DONE) {
        VLOG_ERROR("served", "served_state_remove_application: failed to delete application: %s\n", sqlite3_errmsg(g_state->database));
        return -1;
    }

    // Remove from in-memory state
    for (int i = 0; i < g_state->applications_states_count; i++) {
        if (g_state->applications_states[i].name && 
            strcmp(g_state->applications_states[i].name, application->name) == 0) {
            
            __state_application_delete(&g_state->applications_states[i]);
            
            // Shift remaining applications
            for (int j = i; j < g_state->applications_states_count - 1; j++) {
                g_state->applications_states[j] = g_state->applications_states[j + 1];
            }
            g_state->applications_states_count--;
            served_state_mark_dirty();
            break;
        }
    }

    return 0;
}

int served_state_get_applications(struct state_application** applicationsOut, int* applicationsCount)
{
    if (g_state == NULL || applicationsOut == NULL || applicationsCount == NULL) {
        return -1;
    }

    *applicationsOut = g_state->applications_states;
    *applicationsCount = g_state->applications_states_count;
    return 0;
}

int served_state_get_transaction_states(struct state_transaction** transactionsOut, int* transactionsCount)
{
    if (g_state == NULL || transactionsOut == NULL || transactionsCount == NULL) {
        return -1;
    }

    *transactionsOut = g_state->transaction_states;
    *transactionsCount = g_state->transaction_state_count;
    return 0;
}
