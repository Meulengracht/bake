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

#ifndef __SERVED_TRANSACTION_LOGGING_H__
#define __SERVED_TRANSACTION_LOGGING_H__

#include <chef/list.h>
#include <transaction/sm.h>
#include <time.h>

enum served_transaction_log_level {
    SERVED_TRANSACTION_LOG_INFO,
    SERVED_TRANSACTION_LOG_WARNING,
    SERVED_TRANSACTION_LOG_ERROR
};

struct served_transaction_log_entry {
    struct list_item                  list_header;
    enum served_transaction_log_level level;
    time_t                            timestamp;
    sm_state_t                        state;
    char                              message[512];
};

extern void served_transaction_log(
    struct served_transaction*        transaction,
    enum served_transaction_log_level level,
    const char*                       format,
    ...
);

#define TXLOG_INFO(tx, ...)    served_transaction_log(tx, SERVED_TRANSACTION_LOG_INFO, __VA_ARGS__)
#define TXLOG_WARNING(tx, ...) served_transaction_log(tx, SERVED_TRANSACTION_LOG_WARNING, __VA_ARGS__)
#define TXLOG_ERROR(tx, ...)   served_transaction_log(tx, SERVED_TRANSACTION_LOG_ERROR, __VA_ARGS__)

#endif //!__SERVED_TRANSACTION_LOGGING_H__
