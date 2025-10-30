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

#ifndef __SERVED_TRANSACTION_STATE_LOAD_H__
#define __SERVED_TRANSACTION_STATE_LOAD_H__

#include "../sm.h"
#include "types.h"

extern enum sm_action_result served_handle_state_load(void* context);

static const struct served_sm_state g_stateLoad = {
    .state = SERVED_TX_STATE_LOAD,
    .action = served_handle_state_load,
    .transition_count = 2,
    .transitions = {
        { SERVED_TX_EVENT_OK,     SERVED_TX_STATE_START_SERVICES },
        { SERVED_TX_EVENT_FAILED, SERVED_TX_STATE_ERROR }
    }
};

#endif //!__SERVED_TRANSACTION_STATE_LOAD_H__
