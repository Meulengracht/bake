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
struct containerv_container;

struct state_transaction {
    unsigned int id;

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
    struct containerv_container* container;
};


/**
 * @brief
 *
 * @return
 */
extern int served_state_load(void);

/**
 * @brief
 *
 * @return
 */
extern int served_state_save(void);

/**
 *
 * @return
 */
extern int served_state_lock(void);

/**
 *
 * @return
 */
extern int served_state_unlock(void);

extern unsigned int served_state_transaction_new(struct state_transaction* state);

extern struct state_transaction* served_state_transaction(unsigned int id);

extern struct state_application* served_state_application(const char* name);

extern int served_state_add_application(struct state_application* application);

/**
 * @brief
 *
 * @param applicationsOut
 * @param applicationsCount
 * @return
 */
extern int served_state_get_applications(struct served_application*** applicationsOut, int* applicationsCount);

/**
 * @brief 
 * 
 * @param application 
 * @return int 
 */
extern int served_state_remove_application(struct served_application* application);

#endif //!__SERVED_STATE_H__
