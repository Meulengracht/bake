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

#include <chef/platform.h>
#include <server.h>
#include <vlog.h>

void chef_cvd_create_invocation(struct gracht_message* message, const struct chef_create_parameters* params)
{
    enum chef_status status;
    const char*      id = "";
    VLOG_DEBUG("api", "create(layers=%u)\n", params->layers_count);

    status = cvd_create(params, &id);
    chef_cvd_create_response(message, (char*)id, status);
}

void chef_cvd_spawn_invocation(struct gracht_message* message, const struct chef_spawn_parameters* params)
{
    enum chef_status status;
    unsigned int     pID;
    VLOG_DEBUG("api", "spawn(id=%s, command=%s)\n", params->container_id, params->command);

    status = cvd_spawn(params, &pID);
    chef_cvd_spawn_response(message, pID, status);
}

void chef_cvd_kill_invocation(struct gracht_message* message, const char* container_id, const unsigned int pid)
{
    VLOG_DEBUG("api", "kill(id=%s, pid=%u)\n", container_id, pid);
    chef_cvd_kill_response(message, cvd_kill(container_id, pid));
}

void chef_cvd_upload_invocation(struct gracht_message* message, const struct chef_file_parameters* params)
{
    VLOG_DEBUG("api", "upload(id=%s, source=%s, dest=%s)\n", params->container_id, params->source_path, params->destination_path);
    chef_cvd_upload_response(message, cvd_transfer(params, CVD_TRANSFER_UPLOAD));
}

void chef_cvd_download_invocation(struct gracht_message* message, const struct chef_file_parameters* params)
{
    VLOG_DEBUG("api", "download(id=%s, source=%s, dest=%s)\n", params->container_id, params->source_path, params->destination_path);
    chef_cvd_download_response(message, cvd_transfer(params, CVD_TRANSFER_DOWNLOAD));
}

void chef_cvd_destroy_invocation(struct gracht_message* message, const char* container_id)
{
    VLOG_DEBUG("api", "destroy(id=%s)\n", container_id);
    chef_cvd_destroy_response(message, cvd_destroy(container_id));
}
