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

#ifndef __SERVED_STATE_H__
#define __SERVED_STATE_H__

#include <chef/bits/package.h>
#include <transaction/transaction.h>
#include <transaction/logging.h>

// forwards
struct served_mount;

/**
 * @brief Represents a log entry for a transaction.
 */
struct state_transaction_log {
    enum served_transaction_log_level level;     /**< Log level (INFO/WARNING/ERROR) */
    time_t                            timestamp; /**< When the log was created */
    sm_state_t                        state;     /**< Transaction state when logged */
    char                              message[512]; /**< Log message */
};

/**
 * @brief Represents a transaction state entry that tracks package operations.
 * 
 * This structure stores information about a package transaction, including
 * the package name, channel, and revision number. Transaction states are
 * persisted to disk and used to track ongoing operations.
 */
struct state_transaction {
    unsigned int id;        /**< Unique transaction identifier */

    // Package information
    const char* name;       /**< Package name */
    const char* channel;    /**< Distribution channel (e.g., "stable", "beta") */
    int         revision;   /**< Package revision number */
    
    // Transaction logs
    struct state_transaction_log* logs;       /**< Array of log entries */
    int                           logs_count; /**< Number of log entries */
};

/**
 * @brief Represents a specific revision of an installed application.
 * 
 * Tracks the version information for a particular revision of an application,
 * including which channel it's tracking for updates.
 */
struct state_application_revision {
    const char*          tracking_channel;  /**< Channel being tracked for updates */
    struct chef_version* version;           /**< Version information for this revision */
};

/**
 * @brief Represents a command that can be executed within an application.
 * 
 * Commands are executable entry points for an application, such as binaries
 * or scripts. Each command has a type, path, and optional arguments.
 */
struct state_application_command {
    const char*            name;        /**< Command name */
    enum chef_command_type type;        /**< Command type (e.g., binary, script) */
    const char*            path;        /**< Relative path to the command executable */
    const char*            arguments;   /**< Default command-line arguments */

    // unserialized members
    unsigned int pid;                   /**< Process ID when command is running (not persisted) */
};

/**
 * @brief Represents an installed application with its commands and revisions.
 * 
 * Applications are installed packages that have been unpacked and configured.
 * Each application can have multiple commands and revisions, and may be
 * mounted in a container for execution.
 */
struct state_application {
    const char* name;                                   /**< Application name (unique identifier) */
    const char* base;                                   /**< Base rootfs for the application */

    struct state_application_command*  commands;        /**< Array of available commands */
    int                                commands_count;  /**< Number of commands */

    struct state_application_revision* revisions;       /**< Array of installed revisions */
    int                                revisions_count; /**< Number of revisions */

    // unserialized members
    const char* container_id;                  /**< Container identifier (not persisted) */
};


/**
 * @brief Loads the state from disk into memory.
 *
 * @return int 0 on success, -1 on failure.
 */
extern int served_state_load(void);

/**
 * @brief Locks the state for exclusive access. This must be called when reading or writing
 * to the state.
 * 
 * State locks can be nested, but each lock must have a corresponding unlock.
 * All state modifications are deferred until the final unlock to provide
 * transactional semantics.
 */
extern void served_state_lock(void);

/**
 * @brief Unlocks the state. A state unlock can cause the state to be saved to disk if it
 * was modified while locked.
 * 
 * When the lock count reaches zero, all deferred database operations are
 * executed atomically within a SQLite transaction. This ensures consistency
 * and batches writes for better performance.
 */
extern void served_state_unlock(void);

/**
 * @brief Saves the current state to disk.
 * 
 * @return int 0 on success, -1 on failure.
 */
extern int served_state_flush(void);

/**
 * @brief Retrieves a transaction by its ID.
 * 
 * @param id The transaction ID to look up
 * @return struct state_transaction* Pointer to the transaction, or NULL if not found
 */
extern struct state_transaction* served_state_transaction(unsigned int id);

/**
 * @brief Retrieves an application by its name.
 * 
 * @param name The application name to look up
 * @return struct state_application* Pointer to the application, or NULL if not found
 */
extern struct state_application* served_state_application(const char* name);

/**
 * @brief Adds a new application to the state. This will mark the state as dirty.
 * 
 * The operation is deferred until served_state_unlock() is called, at which point
 * the application will be persisted to the database. The application is immediately
 * added to the in-memory state for read consistency.
 * 
 * @param application The application to add (must not be NULL)
 * @return int 0 on success, -1 on failure
 */
extern int served_state_add_application(struct state_application* application);

/**
 * @brief Removes an application from the state. This will mark the state as dirty.
 * 
 * The operation is deferred until served_state_unlock() is called. The application
 * is immediately removed from the in-memory state.
 * 
 * @param application The application to remove (must not be NULL)
 * @return int 0 on success, -1 on failure
 */
extern int served_state_remove_application(struct state_application* application);

/**
 * @brief Retrieves all applications in the state.
 *
 * Returns a pointer to the internal application array. The returned pointer
 * is valid only while the state is locked.
 *
 * @param applicationsOut A pointer to receive the application array (must not be NULL)
 * @param applicationsCount A pointer to receive the number of applications (must not be NULL)
 * @return int 0 on success, -1 on failure
 */
extern int served_state_get_applications(struct state_application** applicationsOut, int* applicationsCount);

/**
 * @brief Creates a new transaction with the provided options.
 * 
 * The transaction is added to the state and the operation is deferred until
 * served_state_unlock() is called. The transaction is immediately available
 * in memory for read operations.
 * 
 * @param options Transaction configuration (must not be NULL)
 * @return unsigned int The new transaction ID, or 0 on failure
 */
extern unsigned int served_state_transaction_new(struct served_transaction_options* options);

/**
 * @brief Updates an existing transaction's state.
 * 
 * The update operation is deferred until served_state_unlock() is called.
 * Changes are immediately reflected in the in-memory state.
 * 
 * @param transaction The transaction to update (must not be NULL)
 * @return int The transaction ID on success, -1 on failure
 */
extern int served_state_transaction_update(struct served_transaction* transaction);

/**
 * @brief Adds a log entry to a transaction.
 * 
 * The log entry is immediately added to the in-memory transaction state
 * and persisted to the database when served_state_unlock() is called.
 * 
 * @param transaction_id The ID of the transaction to log to
 * @param level The log level (INFO/WARNING/ERROR)
 * @param timestamp When the log was created
 * @param state The transaction state when the log was created
 * @param message The log message
 * @return int 0 on success, -1 on failure
 */
extern int served_state_transaction_log_add(
    unsigned int transaction_id,
    enum served_transaction_log_level level,
    time_t timestamp,
    sm_state_t state,
    const char* message);

/**
 * @brief Retrieves logs for a transaction.
 * 
 * Returns a pointer to the internal logs array for the transaction.
 * The returned pointer is valid only while the state is locked.
 * 
 * @param transaction_id The ID of the transaction
 * @param logs_out Pointer to receive the logs array (must not be NULL)
 * @param count_out Pointer to receive the number of logs (must not be NULL)
 * @return int 0 on success, -1 on failure
 */
extern int served_state_transaction_logs(
    unsigned int transaction_id,
    struct state_transaction_log** logs_out,
    int* count_out);

/**
 * @brief Retrieves all transactions in the state.
 *
 * Returns a pointer to the internal transaction array. The returned pointer
 * is valid only while the state is locked.
 *
 * @param transactionsOut A pointer to receive the transaction array (must not be NULL)
 * @param transactionsCount A pointer to receive the number of transactions (must not be NULL)
 * @return int 0 on success, -1 on failure
 */
extern int served_state_get_transactions(struct served_transaction** transactionsOut, int* transactionsCount);

/**
 * @brief Creates a new transaction state entry for a specific transaction.
 * 
 * Transaction states track individual package operations within a transaction.
 * The operation is deferred until served_state_unlock() is called, but the
 * state is immediately available in memory.
 * 
 * @param id The transaction ID this state belongs to
 * @param state The transaction state configuration (must not be NULL)
 * @return int 0 on success, -1 on failure
 */
extern int served_state_transaction_state_new(unsigned int id, struct state_transaction* state);

/**
 * @brief Updates an existing transaction state entry.
 * 
 * The update operation is deferred until served_state_unlock() is called.
 * Changes are immediately reflected in the in-memory state.
 * 
 * @param state The transaction state to update (must not be NULL)
 * @return int 0 on success, -1 on failure
 */
extern int served_state_transaction_state_update(struct state_transaction* state);

/**
 * @brief Retrieves all transaction states in the state.
 *
 * Returns a pointer to the internal transaction state array. The returned
 * pointer is valid only while the state is locked.
 *
 * @param transactionsOut A pointer to receive the transaction state array (must not be NULL)
 * @param transactionsCount A pointer to receive the number of transaction states (must not be NULL)
 * @return int 0 on success, -1 on failure
 */
extern int served_state_get_transaction_states(struct state_transaction** transactionsOut, int* transactionsCount);

/**
 * @brief Marks a transaction as completed with the current timestamp.
 * 
 * The operation is deferred until served_state_unlock() is called.
 * 
 * @param id The transaction ID to mark as completed
 * @return int 0 on success, -1 on failure
 */
extern int served_state_transaction_complete(unsigned int id);

/**
 * @brief Cleans up old transactions based on retention policy.
 * 
 * Keeps a minimum of 10 transactions. For transactions beyond that,
 * removes those completed more than 1 week ago.
 * 
 * @return int Number of transactions deleted, or -1 on failure
 */
extern int served_state_transaction_cleanup(void);

#endif //!__SERVED_STATE_H__
