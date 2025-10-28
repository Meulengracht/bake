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

#include "sm.h"

struct served_transaction_wait_cnd {

};

struct served_transaction {
    struct served_sm sm;
    unsigned int     id;
};

extern struct served_transaction* served_transaction_new(unsigned int id, struct served_sm_state_set* stateSet);
extern void served_transaction_delete(struct served_transaction* transaction);

#endif //!__SERVED_TRANSACTION_H__
