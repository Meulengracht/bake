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

#ifndef __CVD_SERVER_H__
#define __CVD_SERVER_H__

#include "chef_cvd_service_server.h"

/**
 * @brief
 */
extern enum chef_status cvd_create(const struct chef_create_parameters* params, const char** id);

/**
 * @brief 
 */
extern enum chef_status cvd_spawn(const struct chef_spawn_parameters* params, unsigned int* pIDOut);

/**
 * @brief
 */
enum cvd_transfer_direction {
    CVD_TRANSFER_UPLOAD,
    CVD_TRANSFER_DOWNLOAD
};

/**
 * @brief
 */
extern enum chef_status cvd_transfer(const struct chef_file_parameters* params, enum cvd_transfer_direction direction);

/**
 * @brief 
 */
extern enum chef_status cvd_kill(const char* containerID, const unsigned int pID);

/**
 * @brief 
 */
extern enum chef_status cvd_destroy(const char* containerID);

/**
 * @brief 
 */
extern int cvd_rootfs_setup_debootstrap(const char* path);


#endif //!__CVD_SERVER_H__
