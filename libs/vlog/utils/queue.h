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

#ifndef __VLOG_QUEUE_H__
#define __VLOG_QUEUE_H__

#include <threads.h>

/**
 * @brief Bounded blocking FIFO queue for pointer-sized items.
 *
 * The queue stores item pointers only and does not take ownership of queued 
 * items and will not destroy them in queue_destroy(). Producers block when 
 * the queue is full; consumers block or time out when it is empty.
 */
struct queue {
    mtx_t lock;
    cnd_t drained;
    cnd_t full;

    int    capacity;
    int    count;
    int    qindex;
    int    dqindex;
    void** items;
};

/**
 * @brief Initializes a bounded queue.
 *
 * @param queue Queue object to initialize.
 * @param capacity Maximum number of pointers that can be queued at once.
 * @return 0 on success, -1 if the backing storage could not be allocated.
 */
int queue_init(struct queue* queue, int capacity);

/**
 * @brief Releases resources owned by a queue.
 *
 * This destroys the synchronization primitives and frees the queue storage.
 * Any payload pointers still stored in the queue remain owned by the caller.
 *
 * @param queue Queue object previously initialized by queue_init().
 */
void queue_destroy(struct queue* queue);

/**
 * @brief Pushes an item into the queue.
 *
 * If the queue is full, this call blocks until a consumer removes an item.
 * Enqueuing NULL is allowed by the implementation, but callers that use
 * queue_pop() timeouts should avoid it because NULL is also the timeout result.
 *
 * @param queue Queue object previously initialized by queue_init().
 * @param item Pointer value to enqueue.
 */
void queue_push(struct queue* queue, void* item);

/**
 * @brief Pops the next item from the queue.
 *
 * If the queue is empty, this call waits until an item is available or until
 * the absolute timeout expires. The timeout follows the C threads
 * cnd_timedwait() contract and must not be NULL for the current implementation.
 *
 * @param queue Queue object previously initialized by queue_init().
 * @param timeout Absolute timeout used while waiting for an item.
 * @return The next queued item, or NULL if the wait timed out.
 */
void* queue_pop(struct queue* queue, struct timespec* timeout);

#endif //!__VLOG_QUEUE_H__
