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
#include "chef_waiterd_cook_service_client.h"
#include <notify.h>
#include <server.h>
#include <vlog.h>

enum chef_build_status __to_protocol_status(enum cookd_notify_build_status cstatus)
{
    switch (cstatus) {
        case COOKD_BUILD_STATUS_QUEUED: return CHEF_BUILD_STATUS_QUEUED;
        case COOKD_BUILD_STATUS_SOURCING: return CHEF_BUILD_STATUS_SOURCING;
        case COOKD_BUILD_STATUS_BUILDING: return CHEF_BUILD_STATUS_BUILDING;
        case COOKD_BUILD_STATUS_PACKING: return CHEF_BUILD_STATUS_PACKING;
        case COOKD_BUILD_STATUS_DONE: return CHEF_BUILD_STATUS_DONE;
        case COOKD_BUILD_STATUS_FAILED: return CHEF_BUILD_STATUS_FAILED;
        default: return CHEF_BUILD_STATUS_UNKNOWN;
    }
}

int cookd_notify_status_update(gracht_client_t* client, const char* id, enum cookd_notify_build_status status)
{
    return chef_waiterd_cook_status(client, NULL, &(struct chef_cook_build_event) {
        .id = (char*)id,
        .status = __to_protocol_status(status)
    });
}

enum chef_artifact_type __to_protocol_atype(enum cookd_notify_artifact_type ctype)
{
    switch (ctype) {
        case COOKD_ARTIFACT_TYPE_LOG: return CHEF_ARTIFACT_TYPE_LOG;
        case COOKD_ARTIFACT_TYPE_PACKAGE: return CHEF_ARTIFACT_TYPE_PACKAGE;
    }
    return CHEF_ARTIFACT_TYPE_LOG;
}

int cookd_notify_artifact_ready(gracht_client_t* client, const char* id, enum cookd_notify_artifact_type type, const char* uri)
{
    return chef_waiterd_cook_artifact(client, NULL, &(struct chef_cook_artifact_event) {
        .id = (char*)id,
        .type = __to_protocol_atype(type),
        .uri = (char*)uri
    });
}