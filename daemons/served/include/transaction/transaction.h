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

#ifndef __SERVED_TRANSACTION_H__
#define __SERVED_TRANSACTION_H__

#include <chef/list.h>
#include <time.h>
#include "sm.h"

enum served_transaction_type {
    SERVED_TRANSACTION_TYPE_EPHEMERAL,
    SERVED_TRANSACTION_TYPE_INSTALL,
    SERVED_TRANSACTION_TYPE_UNINSTALL,
    SERVED_TRANSACTION_TYPE_UPDATE,
    SERVED_TRANSACTION_TYPE_ROLLBACK,
    SERVED_TRANSACTION_TYPE_CONFIGURE
};

// TODO:
// 1. vlog streams that we can attach to transactions
// 2. transaction progress reporting (downloading, io)
// 3. Reboot handling of transactions (i.e generate boot ids in /tmp and keep track)

enum served_transaction_wait_type {
    SERVED_TRANSACTION_WAIT_TYPE_NONE,
    SERVED_TRANSACTION_WAIT_TYPE_TRANSACTION,
    SERVED_TRANSACTION_WAIT_TYPE_REBOOT
};

struct served_transaction_wait {
    enum served_transaction_wait_type type;
    union {
        unsigned int transaction_id;
    } data;
};

struct served_transaction {
    struct list_item               list_header;
    struct served_sm               sm;
    unsigned int                   id;
    const char*                    name;
    const char*                    description;
    enum served_transaction_type   type;
    struct served_transaction_wait wait;
    time_t                         created_at;
    time_t                         completed_at;
    
    // I/O progress tracking
    struct {
        unsigned long long bytes_current;
        unsigned long long bytes_total;
        unsigned int       last_reported_percentage;
    } io_progress;
};

struct served_transaction_options {
    const char*                    name;
    const char*                    description;
    enum served_transaction_type   type;

    // optional, can only be used for ephemeral transactions
    struct served_sm_state_set*    stateSet;

    // Restoration/initialization fields
    unsigned int                   id;
    int                            initialState;
    struct served_transaction_wait wait;
};

extern struct served_transaction* served_transaction_new(struct served_transaction_options* options);
extern void                       served_transaction_construct(struct served_transaction* transaction, struct served_transaction_options* options);
extern void                       served_transaction_delete(struct served_transaction* transaction);

#endif //!__SERVED_TRANSACTION_H__
