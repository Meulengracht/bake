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

#ifndef __SERVED_TRANSACTION_STATE_DOWNLOAD_H__
#define __SERVED_TRANSACTION_STATE_DOWNLOAD_H__

#include "../sm.h"
#include "types.h"

extern enum sm_action_result served_handle_state_download(void* context);
extern enum sm_action_result served_handle_state_download_retry(void* context);

static const struct served_sm_state g_stateDownload = {
    .state = SERVED_TX_STATE_DOWNLOAD,
    .action = served_handle_state_download,
    .transition_count = 3,
    .transitions = {
        { SERVED_TX_EVENT_OK,     SERVED_TX_STATE_DOWNLOAD },
        { SERVED_TX_EVENT_RETRY,  SERVED_TX_STATE_DOWNLOAD_RETRY },
        { SERVED_TX_EVENT_FAILED, SERVED_TX_STATE_ERROR }
    }
};

static const struct served_sm_state g_stateDownloadRetry = {
    .state = SERVED_TX_STATE_DOWNLOAD_RETRY,
    .action = served_handle_state_download_retry,
    .transition_count = 1,
    .transitions = {
        { SERVED_TX_EVENT_OK, SERVED_TX_STATE_DOWNLOAD }
    }
};

#endif //!__SERVED_TRANSACTION_STATE_PRECHECK_H__
