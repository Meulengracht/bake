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

#ifndef __SERVED_TRANSACTION_STATE_MOUNT_H__
#define __SERVED_TRANSACTION_STATE_MOUNT_H__

#include "../sm.h"
#include "types.h"

extern enum sm_action_result served_handle_state_mount(void* context);
extern enum sm_action_result served_handle_state_mount_all(void* context);

static const struct served_sm_state g_stateMount = {
    .state = SERVED_TX_STATE_MOUNT,
    .action = served_handle_state_mount,
    .transition_count = 3,
    .transitions = {
        { SERVED_TX_EVENT_OK,     SERVED_TX_STATE_LOAD },
        { SERVED_TX_EVENT_FAILED, SERVED_TX_STATE_ERROR },
        { SERVED_TX_EVENT_CANCEL, SERVED_TX_STATE_CANCELLED }
    }
};

static const struct served_sm_state g_stateMountAll = {
    .state = SERVED_TX_STATE_MOUNT,
    .action = served_handle_state_mount_all,
    .transition_count = 3,
    .transitions = {
        { SERVED_TX_EVENT_OK,     SERVED_TX_STATE_LOAD },
        { SERVED_TX_EVENT_FAILED, SERVED_TX_STATE_ERROR },
        { SERVED_TX_EVENT_CANCEL, SERVED_TX_STATE_CANCELLED }
    }
};

#endif //!__SERVED_TRANSACTION_STATE_MOUNT_H__
