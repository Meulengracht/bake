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

#ifndef __CHEF_REMOTE_H__
#define __CHEF_REMOTE_H__

#include <gracht/client.h>

/**
 * @brief 
 */
extern int remote_wizard_init(void);

/**
 * @brief
 */
extern int remote_local_init_default(void);

/**
 * @brief
 */
extern int remote_pack(const char* path, const char* const* envp, char** imagePath);

/**
 * @brief
 */
extern int remote_unpack(const char* imagePath, const char* destination);

/**
 * @brief
 */
extern int remote_upload(const char* path, char** downloadUrl);

/**
 * @brief
 */
extern int remote_download(const char* url, const char* path);

/**
 * @brief 
 */
extern int remote_client_create(gracht_client_t** clientOut);

#endif //!__CHEF_REMOTE_H__
