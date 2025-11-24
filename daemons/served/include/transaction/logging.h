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

// Transaction log levels
enum served_transaction_log_level {
    SERVED_TRANSACTION_LOG_INFO,
    SERVED_TRANSACTION_LOG_WARNING,
    SERVED_TRANSACTION_LOG_ERROR
};

// Individual log entry
struct served_transaction_log_entry {
    struct list_item                  list_header;
    enum served_transaction_log_level level;
    time_t                            timestamp;
    sm_state_t                        state;
    char                              message[512];
};

// Transaction logging
extern void served_transaction_log(
    struct served_transaction* transaction,
    enum served_transaction_log_level level,
    const char* format,
    ...
) __attribute__((format(printf, 3, 4)));

extern void served_transaction_log_info(struct served_transaction* transaction, const char* format, ...) __attribute__((format(printf, 2, 3)));
extern void served_transaction_log_warning(struct served_transaction* transaction, const char* format, ...) __attribute__((format(printf, 2, 3)));
extern void served_transaction_log_error(struct served_transaction* transaction, const char* format, ...) __attribute__((format(printf, 2, 3)));

#endif //!__SERVED_TRANSACTION_LOGGING_H__
