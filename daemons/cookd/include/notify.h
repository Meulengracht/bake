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

#ifndef __COOKD_NOTIFY_H__
#define __COOKD_NOTIFY_H__

#include <gracht/client.h>

enum cookd_notify_build_status {
    COOKD_BUILD_STATUS_QUEUED,
    COOKD_BUILD_STATUS_SOURCING,
    COOKD_BUILD_STATUS_BUILDING,
    COOKD_BUILD_STATUS_PACKING,
    COOKD_BUILD_STATUS_DONE,
    COOKD_BUILD_STATUS_FAILED,
};

/**
 * @brief
 */
extern int cookd_notify_status_update(gracht_client_t* client, const char* id, enum cookd_notify_build_status status);

enum cookd_notify_artifact_type {
    COOKD_ARTIFACT_TYPE_LOG,
    COOKD_ARTIFACT_TYPE_PACKAGE,
};

/**
 * @brief
 */
extern int cookd_notify_artifact_ready(gracht_client_t* client, const char* id, enum cookd_notify_artifact_type type, const char* uri);

#endif //!__COOKD_NOTIFY_H__
