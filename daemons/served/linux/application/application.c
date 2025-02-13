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

#include <application.h>
#include <stdlib.h>
#include <string.h>
#include <vlog.h>

struct served_application* served_application_new(void)
{
    struct served_application* application = (struct served_application*)malloc(sizeof(struct served_application));
    if (application == NULL) {
        return NULL;
    }

    memset(application, 0, sizeof(struct served_application));
    return application;
}

void served_application_delete(struct served_application* application)
{
    if (application == NULL) {
        return;
    }

    for (int i = 0; i < application->commands_count; i++) {
        struct served_command* command = &application->commands[i];
        free((void*)command->name);
        free((void*)command->path);
        free((void*)command->arguments);
    }
    free((void*)application->commands);
    free((void*)application->package);
    free((void*)application->publisher);
    free((void*)application->name);
    free(application);
}

int served_application_load(struct served_application* application)
{
    int status;
    VLOG_DEBUG("app", "served_application_load(name=%s)\n", application->name);

    status = served_application_ensure_paths(application);
    if (status != 0) {
        VLOG_ERROR("app", "failed to create application paths\n");
        return status;
    }

    status = served_application_mount(application);
    if (status != 0) {
        VLOG_ERROR("app", "failed to mount application\n");
        return status;
    }

    status = served_application_start_daemons(application);
    if (status != 0) {
        VLOG_ERROR("app", "failed to start application daemons\n");
        return status;
    }
    return 0;
}

int served_application_unload(struct served_application* application)
{
    int status;
    VLOG_DEBUG("app", "served_application_unload(name=%s)\n", application->name);

    status = served_application_stop_daemons(application);
    if (status != 0) {
        VLOG_ERROR("app", "failed to stop application daemons\n");
        return status;
    }

    status = served_application_unmount(application);
    if (status != 0) {
        VLOG_ERROR("app", "failed to unmount application\n");
        return status;
    }
    return 0;
}
