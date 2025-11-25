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

#ifndef __SERVED_STATE_RUNNER_H__
#define __SERVED_STATE_RUNNER_H__

#include <transaction/transaction.h>

/**
 * @brief Starts the runner thread. The runner will continuously process transactions
 * in the background until served_runner_stop() is called.
 * 
 * @return int 0 on success, -1 on failure
 */
extern int served_runner_start(void);

/**
 * @brief Requests the runner thread to stop and waits for it to complete.
 * This should be called during shutdown to ensure graceful termination.
 * 
 * @return int 0 on success, -1 on failure
 */
extern int served_runner_stop(void);

/**
 * @brief Checks if the runner thread is currently running.
 * 
 * @return int 1 if running, 0 if stopped
 */
extern int served_runner_is_running(void);

/**
 * @brief Creates a new transaction and registers it with the runner.
 * 
 * The created transaction will be managed and executed by the runner.
 * 
 * @param options Configuration options for the new transaction
 * @return unsigned int The ID of the newly created transaction, or 0 on failure
 */
extern unsigned int served_transaction_create(struct served_transaction_options* options);

/**
 * @brief Maps an internal transaction state to the protocol state enum.
 * 
 * @param state The internal state machine state
 * @return enum chef_transaction_state The corresponding protocol state
 */
extern enum chef_transaction_state served_transaction_map_state(sm_state_t state);

#endif // __SERVED_STATE_RUNNER_H__
