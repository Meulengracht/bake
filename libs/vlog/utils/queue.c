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

#include "queue.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int queue_init(struct queue* queue, int capacity)
{
    memset(queue, 0, sizeof(struct queue));
    queue->items = calloc(capacity, sizeof(void*));
    if (queue->items == NULL) {
        return -1;
    }

    mtx_init(&queue->lock, mtx_plain);
    cnd_init(&queue->drained);
    cnd_init(&queue->full);
    queue->capacity = capacity;
    return 0;
}

void queue_destroy(struct queue* queue)
{
    if (queue->items) {
        free(queue->items);
        queue->items = NULL;
    }
    cnd_destroy(&queue->drained);
    cnd_destroy(&queue->full);
    mtx_destroy(&queue->lock);
}

void queue_push(struct queue* queue, void* item)
{
    mtx_lock(&queue->lock);
    while (queue->count == queue->capacity) {
        cnd_wait(&queue->full, &queue->lock);
    }

    queue->items[queue->qindex] = item;
    queue->qindex = (queue->qindex + 1) % queue->capacity;
    queue->count++;

    cnd_signal(&queue->drained);
    mtx_unlock(&queue->lock);
}

void* queue_pop(struct queue* queue, struct timespec* timeout)
{
    void* item = NULL;

    mtx_lock(&queue->lock);
    while (queue->count == 0) {
        if (cnd_timedwait(&queue->drained, &queue->lock, timeout) == thrd_timedout) {
            mtx_unlock(&queue->lock);
            return NULL;
        }
    }

    item = queue->items[queue->dqindex];
    queue->dqindex = (queue->dqindex + 1) % queue->capacity;
    queue->count--;

    cnd_signal(&queue->full);
    mtx_unlock(&queue->lock);

    return item;
}
