/**
 * Copyright 2022, Philip Meulengracht
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

#ifndef __LIBPLATFORM_LIST_H__
#define __LIBPLATFORM_LIST_H__

#include <stddef.h>

struct list_item {
    struct list_item* next;
    struct list_item* prev;
};

struct list {
    struct list_item* head;
    struct list_item* tail;
    int count;
};

#define list_foreach(list, item) \
    for (item = (list)->head; item != NULL; item = item->next)

static inline void list_init(struct list* list)
{
    list->head = NULL;
    list->tail = NULL;
    list->count = 0;
}

static inline void list_add(struct list* list, struct list_item* item)
{
    // initialize element
    item->next = NULL;
    item->prev = NULL;

    if (list->head == NULL) {
        list->head = item;
        list->tail = item;
    }
    else {
        list->tail->next = item;
        item->prev = list->tail;
        list->tail = item;
    }
    list->count++;
}

static inline void list_remove(struct list* list, struct list_item* item)
{
    if (item->prev != NULL)
        item->prev->next = item->next;
    else
        list->head = item->next;
    if (item->next != NULL)
        item->next->prev = item->prev;
    else
        list->tail = item->prev;
    list->count--;
}

#endif //!__LIBPLATFORM_LIST_H__
