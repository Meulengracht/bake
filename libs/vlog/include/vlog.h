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

#ifndef __VLOG_H__
#define __VLOG_H__

#include <stdio.h>

/**
 * @brief Controls which log events are emitted to a sink.
 */
enum vlog_level {
    VLOG_LEVEL_DISABLED,
    VLOG_LEVEL_ERROR,
    VLOG_LEVEL_WARNING,
    VLOG_LEVEL_TRACE,
    VLOG_LEVEL_DEBUG
};

/**
 * @brief Identifies the built-in presentation sinks supported by vlog.
 */
enum vlog_sink_type {
    VLOG_SINK_TYPE_TEXT,
    VLOG_SINK_TYPE_VIEW
};

/**
 * @brief Emits an error event and terminates the process with EXIT_FAILURE.
 */
#define VLOG_FATAL(tag, ...)   vlog_output(VLOG_LEVEL_ERROR, tag, __VA_ARGS__); exit(EXIT_FAILURE)

/**
 * @brief Emits an error event.
 */
#define VLOG_ERROR(tag, ...)   vlog_output(VLOG_LEVEL_ERROR, tag, __VA_ARGS__)

/**
 * @brief Emits a warning event.
 */
#define VLOG_WARNING(tag, ...) vlog_output(VLOG_LEVEL_WARNING, tag, __VA_ARGS__)

/**
 * @brief Emits a user-facing trace event.
 */
#define VLOG_TRACE(tag, ...)   vlog_output(VLOG_LEVEL_TRACE, tag, __VA_ARGS__)

/**
 * @brief Emits a debug event.
 */
#define VLOG_DEBUG(tag, ...)   vlog_output(VLOG_LEVEL_DEBUG, tag, __VA_ARGS__)

/**
 * @brief Closes the sink FILE* when the sink is destroyed.
 */
#define VLOG_OUTPUT_OPTION_CLOSE    0x1

/**
 * @brief Clears the current terminal line before writing text output.
 */
#define VLOG_OUTPUT_OPTION_PROGRESS 0x2

/**
 * @brief Writes only the event message, without level/tag decorations.
 */
#define VLOG_OUTPUT_OPTION_NODECO   0x4

/**
 * @brief Uses long text decorations with timestamp, level, and tag fields.
 */
#define VLOG_OUTPUT_OPTION_LONGDECO 0x8

/**
 * @brief Initializes vlog and starts the renderer thread.
 *
 * This must be called before any other vlog_* function. The default sink level
 * is set to @p level and a text sink for stdout is added automatically.
 *
 * @param level Default minimum level for subsequently added sinks.
 */
extern void vlog_initialize(enum vlog_level level);

/**
 * @brief Shuts down the renderer thread and releases vlog resources.
 *
 * Pending events are flushed before the renderer exits. Sinks created with
 * VLOG_OUTPUT_OPTION_CLOSE close their FILE* handles during destruction.
 *
 */
extern void vlog_cleanup(void);

/**
 * @brief Sets the logging level for all active sinks and future sinks.
 *
 * The active sink update is queued for the renderer thread. The default level
 * used by future sink creation is updated immediately.
 *
 * @param level Maximum verbosity accepted by each sink.
 */
extern void vlog_set_level(enum vlog_level level);

/**
 * @brief Adds a plain text sink for log events.
 *
 * The sink is created asynchronously on the renderer thread. Text sinks write
 * durable log lines to @p output and ignore view-only events.
 *
 * @param output FILE* that receives text log output.
 * @param close Non-zero to close @p output when the sink is destroyed.
 * @return 0 if the add event was queued, -1 if the event could not be allocated.
 */
extern int vlog_sink_add_text(FILE* output, int close);

/**
 * @brief Adds an interactive terminal view sink.
 *
 * The sink is created asynchronously on the renderer thread. View sinks own the
 * progressive step model for their terminal and render retained status rows.
 *
 * @param output FILE* for the terminal view.
 * @param close Non-zero to close @p output when the sink is destroyed.
 * @return 0 if the add event was queued, -1 if the event could not be allocated.
 */
extern int vlog_sink_add_view(FILE* output, int close);

/**
 * @brief Removes sinks attached to a FILE*.
 *
 * Removal is queued for the renderer thread. Any matching sink is destroyed
 * there, including closing its FILE* when configured to do so.
 *
 * @param output FILE* whose sinks should be removed.
 * @return 0 if the remove event was queued, -1 if the event could not be allocated.
 */
extern int vlog_sink_remove(FILE* output);

/**
 * @brief Enables output option flags for sinks attached to a FILE*.
 *
 * The option update is queued for the renderer thread.
 *
 * @param output FILE* identifying the sinks to update.
 * @param flags Bitmask of VLOG_OUTPUT_OPTION_* values to enable.
 */
extern void vlog_set_output_options(FILE* output, unsigned int flags);

/**
 * @brief Disables output option flags for sinks attached to a FILE*.
 *
 * The option update is queued for the renderer thread.
 *
 * @param output FILE* identifying the sinks to update.
 * @param flags Bitmask of VLOG_OUTPUT_OPTION_* values to disable.
 */
extern void vlog_clear_output_options(FILE* output, unsigned int flags);

/**
 * @brief Sets the logging level for sinks attached to a FILE*.
 *
 * The level update is queued for the renderer thread.
 *
 * @param output FILE* identifying the sinks to update.
 * @param level Maximum verbosity accepted by the matching sinks.
 */
extern void vlog_set_output_level(FILE* output, enum vlog_level level);

/**
 * @brief Emits a formatted log event.
 *
 * The message is formatted and copied before being queued. Sinks decide how the
 * event is presented: text sinks write durable lines, while view sinks can clear
 * and redraw their retained progress view around the line.
 *
 * @param level Level associated with the event.
 * @param tag Short subsystem or scope tag for the event.
 * @param format printf-style format string.
 * @param ... Arguments for @p format.
 */
extern void vlog_output(enum vlog_level level, const char* tag, const char* format, ...);

/**
 * @brief Blocks until previously queued events are rendered and flushed.
 *
 * This acts as a synchronization barrier with the renderer thread.
 */
extern void vlog_flush(void);


/**
 * @brief Status values used by progressive step events.
 */
enum vlog_content_status_type {
    VLOG_CONTENT_STATUS_NONE,
    VLOG_CONTENT_STATUS_WAITING,
    VLOG_CONTENT_STATUS_WORKING,
    VLOG_CONTENT_STATUS_DONE,
    VLOG_CONTENT_STATUS_FAILED
};

/**
 * @brief Opens a retained progressive view for a terminal sink.
 *
 * A view sink is added for @p handle if needed. If @p handle is not a terminal,
 * this call is ignored. The header and footer strings are copied before being
 * queued.
 *
 * @param handle Terminal FILE* that should render the view.
 * @param header Title displayed at the top of the view.
 * @param footer Footer displayed at the bottom of the view.
 */
extern void vlog_view_open(FILE* handle, const char* header, const char* footer);

/**
 * @brief Closes any active progressive view.
 *
 * The close event is queued for the renderer thread. The final view state is
 * rendered before the view is marked inactive.
 */
extern void vlog_view_close(void);

/**
 * @brief Opaque handle for a logical progressive operation.
 *
 * Step IDs are assigned by vlog_step_open() and are used by sinks to associate
 * later update and close events with the same operation.
 */
struct vlog_step {
    unsigned int id;
};

/**
 * @brief Opens a progressive step and assigns its ID.
 *
 * The step label is copied before being queued. A view sink renders opened
 * steps as retained rows in open order.
 *
 * @param step Step handle to initialize.
 * @param label Human-readable step label.
 * @return 0 if the open event was queued, -1 on invalid input or allocation failure.
 */
extern int vlog_step_open(struct vlog_step* step, const char* label);

/**
 * @brief Updates the status and optional message for an open step.
 *
 * @param step Step handle previously initialized by vlog_step_open().
 * @param status New step status.
 * @param format Optional printf-style message format; may be NULL.
 * @param ... Arguments for @p format.
 * @return 0 if the update event was queued, -1 on invalid input or allocation failure.
 */
extern int vlog_step_update(struct vlog_step* step,
                     enum vlog_content_status_type status,
                     const char* format, ...);

/**
 * @brief Closes a progressive step with a final status and optional message.
 *
 * @param step Step handle previously initialized by vlog_step_open().
 * @param status Final step status, normally VLOG_CONTENT_STATUS_DONE or
 * VLOG_CONTENT_STATUS_FAILED.
 * @param format Optional printf-style final message format; may be NULL.
 * @param ... Arguments for @p format.
 * @return 0 if the close event was queued, -1 on invalid input or allocation failure.
 */
extern int vlog_step_close(struct vlog_step* step,
                    enum vlog_content_status_type status,
                    const char* format, ...);

#endif //!__VLOG_H__
