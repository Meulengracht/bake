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

#ifndef __SERVED_TRANSACTION_STATE_DEPENDENCIES_H__
#define __SERVED_TRANSACTION_STATE_DEPENDENCIES_H__

#include "../sm.h"
#include "types.h"

extern enum sm_action_result served_handle_state_dependencies(void* context);
extern enum sm_action_result served_handle_state_dependencies_wait(void* context);

static const struct served_sm_state g_stateDependencies = {
    .state = SERVED_TX_STATE_DEPENDENCIES,
    .action = served_handle_state_dependencies,
    .transition_count = 3,
    .transitions = {
        { SERVED_TX_EVENT_WAIT,   SERVED_TX_STATE_DEPENDENCIES_WAIT },
        { SERVED_TX_EVENT_OK,     SERVED_TX_STATE_INSTALL }, // When installing
        { SERVED_TX_EVENT_OK,     SERVED_TX_STATE_REMOVE_WRAPPERS }, // When updating
        { SERVED_TX_EVENT_FAILED, SERVED_TX_STATE_ERROR }
    }
};

static const struct served_sm_state g_stateDependenciesWait = {
    .state = SERVED_TX_STATE_DEPENDENCIES_WAIT,
    .action = served_handle_state_dependencies_wait,
    .transition_count = 2,
    .transitions = {
        { SERVED_TX_EVENT_OK,     SERVED_TX_STATE_INSTALL }, // When installing
        { SERVED_TX_EVENT_OK,     SERVED_TX_STATE_REMOVE_WRAPPERS }, // When updating
        { SERVED_TX_EVENT_FAILED, SERVED_TX_STATE_ERROR }
    }
};

#endif //!__SERVED_TRANSACTION_STATE_DEPENDENCIES_H__
