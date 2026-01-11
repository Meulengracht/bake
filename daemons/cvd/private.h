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

#ifndef __CVD_PRIVATE_H__
#define __CVD_PRIVATE_H__

#include <gracht/server.h>

struct cvd_config_address {
    const char*    type;
    const char*    address;
    unsigned short port;
};

struct config_custom_path {
    const char* path;
    int         access;  // bitwise OR of CV_FS_READ, CV_FS_WRITE, CV_FS_EXEC
};

/**
 * @brief
 */
extern int cvd_config_load(const char* confdir);

/**
 * @brief
 */
extern void cvd_config_destroy(void);

/**
 * @brief 
 */
extern void cvd_config_api_address(struct cvd_config_address* address);

/**
 * @brief Get the default security policy from configuration
 * @return The default policy name (e.g., "minimal", "build", "network") or NULL
 */
extern const char* cvd_config_security_default_policy(void);

/**
 * @brief Get custom paths from security configuration
 * @param paths Output pointer to custom paths array (can be NULL)
 * @param count Output pointer to number of custom paths (can be NULL)
 */
extern void cvd_config_security_custom_paths(struct config_custom_path** paths, size_t* count);

/**
 * @brief
 */
extern int cvd_initialize_server(struct gracht_server_configuration* config, gracht_server_t** serverOut);

#endif //!__CVD_PRIVATE_H__
