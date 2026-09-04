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

#ifndef __VLOG_SINKS_H__
#define __VLOG_SINKS_H__

#include <vlog.h>

struct vlog_tty_step {
    unsigned int                  id;
    char*                         label;
    char*                         message;
    enum vlog_content_status_type status;
};

struct vlog_sink {
    enum vlog_sink_type type;
    FILE*               handle;
    enum vlog_level     level;
    void (*emit)(struct vlog_sink*, const struct vlog_event*);
    void (*tick)(struct vlog_sink*, long long time);
    void (*flush)(struct vlog_sink*);
    void (*destroy)(struct vlog_sink*, unsigned int ignoreClose);
};

struct vlog_sink_text {
    struct vlog_sink base;
    unsigned int     options;
};

struct vlog_sink_tty {
    struct vlog_sink base;
    unsigned int options;
    int          close;

    int columns;
    int view_active;
    int rendered_rows;

    // The content of the view
    char*                     title;
    char*                     footer;
    struct vlog_tty_step*     steps;
    size_t                    step_count;
    size_t                    step_capacity;
    unsigned long long        active_step_id;

    // Spinner information
    long long          spinner_time_ms;
    int                spinner_index;
};

extern struct vlog_sink* vlog_sink_new_text(FILE* handle, enum vlog_level level, unsigned int options);
extern struct vlog_sink* vlog_sink_new_view(FILE* handle, enum vlog_level level, unsigned int options);

#endif //!__VLOG_SINKS_H__
