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

#ifndef __SERVED_TRANSACTION_STATE_COMPLETED_H__
#define __SERVED_TRANSACTION_STATE_COMPLETED_H__

#include "../sm.h"
#include "types.h"

extern enum sm_action_result served_handle_state_completed(void* context);

static const struct served_sm_state g_stateCompleted = {
    .state = SERVED_TX_STATE_COMPLETED,
    .action = served_handle_state_completed,
    .transition_count = 0,
    .transitions = { },
};

#endif //!__SERVED_TRANSACTION_STATE_COMPLETED_H__
