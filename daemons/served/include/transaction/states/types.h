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

#ifndef __SERVED_TRANSACTION_STATE_TYPES_H__
#define __SERVED_TRANSACTION_STATE_TYPES_H__

#include "../sm.h"

#define SERVED_TX_STATE_PRECHECK          (sm_state_t)0
#define SERVED_TX_STATE_PRECHECK_WAIT     (sm_state_t)1
#define SERVED_TX_STATE_DOWNLOAD          (sm_state_t)2
#define SERVED_TX_STATE_DOWNLOAD_RETRY    (sm_state_t)3
#define SERVED_TX_STATE_VERIFY            (sm_state_t)4
#define SERVED_TX_STATE_DEPENDENCIES      (sm_state_t)5
#define SERVED_TX_STATE_DEPENDENCIES_WAIT (sm_state_t)6
#define SERVED_TX_STATE_INSTALL           (sm_state_t)7
#define SERVED_TX_STATE_MOUNT             (sm_state_t)8
#define SERVED_TX_STATE_LOAD              (sm_state_t)9
#define SERVED_TX_STATE_START_SERVICES    (sm_state_t)10
#define SERVED_TX_STATE_GENERATE_WRAPPERS (sm_state_t)11

#define SERVED_TX_STATE_REMOVE_WRAPPERS (sm_state_t)12
#define SERVED_TX_STATE_STOP_SERVICES   (sm_state_t)13
#define SERVED_TX_STATE_UNLOAD          (sm_state_t)14
#define SERVED_TX_STATE_UNMOUNT         (sm_state_t)15
#define SERVED_TX_STATE_UNINSTALL       (sm_state_t)16

#define SERVED_TX_STATE_UPDATE (sm_state_t)17

#define SERVED_TX_STATE_COMPLETED   (sm_state_t)1000
#define SERVED_TX_STATE_ERROR       (sm_state_t)1001
#define SERVED_TX_STATE_CANCELLED   (sm_state_t)1002

#define SERVED_TX_EVENT_OK     (sm_event_t)0
#define SERVED_TX_EVENT_WAIT   (sm_event_t)1
#define SERVED_TX_EVENT_RETRY  (sm_event_t)2
#define SERVED_TX_EVENT_FAILED (sm_event_t)3
#define SERVED_TX_EVENT_CANCEL (sm_event_t)4

#endif //!__SERVED_TRANSACTION_STATE_TYPES_H__
