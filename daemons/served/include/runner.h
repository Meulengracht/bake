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
 * @brief Initializes the transaction runner subsystem.
 */
extern void served_runner_initialize(void);

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
 * @brief Executes all transactions currently registered in the state. It will keep executing
 * transactions until they are either completed, failed, cancelled or waiting for external events.
 */
extern void served_runner_execute(void);

#endif // __SERVED_STATE_RUNNER_H__
