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

// forwards
struct served_mount;

enum state_transaction_flags {
    STATE_TRANSACTION_FLAG_EPHEMERAL = 0x01
};

enum state_transaction_type {
    STATE_TRANSACTION_TYPE_INSTALL,
    STATE_TRANSACTION_TYPE_UNINSTALL,
    STATE_TRANSACTION_TYPE_UPDATE,
    STATE_TRANSACTION_TYPE_ROLLBACK,
    STATE_TRANSACTION_TYPE_CONFIGURE
};

struct state_transaction {
    unsigned int                 id;
    enum state_transaction_type  type;
    enum state_transaction_flags flags;

    // Package information
    const char* name;
    const char* channel;
    int         revision;
};

struct state_application_revision {
    const char*          tracking_channel;
    struct chef_version* version;
};

struct state_application_command {
    const char*            name;
    enum chef_command_type type;
    const char*            path;
    const char*            arguments;

    // unserialized members
    unsigned int pid;
};

struct state_application {
    const char* name;

    struct state_application_command*  commands;
    int                                commands_count;

    struct state_application_revision* revisions;
    int                                revisions_count;

    // unserialized members
    struct served_mount* mount;
    const char*          container_id;
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
 */
extern void served_state_lock(void);

/**
 * @brief Unlocks the state. A state unlock can cause the state to be saved to disk if it
 * was modified while locked.
 */
extern void served_state_unlock(void);

/**
 * @brief Marks the state as dirty, causing it to be saved to disk when unlocked.
 */
extern void served_state_mark_dirty(void);

/**
 * @brief Executes all transactions currently registered in the state. It will keep executing
 * transactions until they are either completed, failed, cancelled or waiting for external events.
 */
extern int served_state_execute(void);

/**
 * @brief Creates a new transaction in the state, with the provided configuration.
 * @param state The transaction configuration.
 * @return unsigned int The transaction ID, or 0 on failure.
 */
extern unsigned int served_state_transaction_new(struct state_transaction* state);

/**
 * @brief Retrieves a transaction by its ID.
 */
extern struct state_transaction* served_state_transaction(unsigned int id);

/**
 * @brief Retrieves an application by its name.
 */
extern struct state_application* served_state_application(const char* name);

/**
 * @brief Adds a new application to the state. This will mark the state as dirty.
 * 
 * @param application the application to add
 * @return int 0 on success, -1 on failure
 */
extern int served_state_add_application(struct state_application* application);

/**
 * @brief Retrieves all applications in the state.
 *
 * @param applicationsOut A pointer to a list of application pointers.
 * @param applicationsCount A pointer to an integer that will receive the number of applications.
 * @return int 0 on success, -1 on failure.
 */
extern int served_state_get_applications(struct state_application*** applicationsOut, int* applicationsCount);

/**
 * @brief Removes an application fromserved_application the state. This will mark the state as dirty.
 * 
 * @param application the application to remove
 * @return int 0 on success, -1 on failure
 */
extern int served_state_remove_application(struct state_application* application);

#endif //!__SERVED_STATE_H__
