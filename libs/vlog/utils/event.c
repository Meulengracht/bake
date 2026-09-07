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

#include "../private.h"
#include <stdlib.h>
#include <time.h>

struct vlog_event* __vlog_event_new(enum vlog_event_type type)
{
    struct vlog_event* event;

    event = calloc(1, sizeof(struct vlog_event));
    if (event == NULL) {
        return NULL;
    }

    event->type = type;
    timespec_get(&event->timestamp, TIME_UTC);
    return event;
}

void __vlog_event_delete(struct vlog_event* event)
{
    if (event) {
        switch (event->type) {
            case VLOG_EVENT_LOG:
                free(event->data.log.tag);
                free(event->data.log.message);
                break;
            case VLOG_EVENT_VIEW_OPEN:
                free(event->data.view_open.header);
                free(event->data.view_open.footer);
                break;
            case VLOG_EVENT_STEP_OPEN:
                free(event->data.step_open.label);
                break;
            case VLOG_EVENT_STEP_UPDATE:
                free(event->data.step_update.message);
                break;
            case VLOG_EVENT_STEP_CLOSE:
                free(event->data.step_close.message);
                break;
            default:
                break;
        }
        free(event);
    }
}

