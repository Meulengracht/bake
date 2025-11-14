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

#ifndef __CHEF_CLIENT_STORAGE_BU_H__
#define __CHEF_CLIENT_STORAGE_BU_H__

/**
 * @brief Uploads a file to blob storage and retrieves the download URL.
 * 
 * This function uploads a file from the local filesystem to blob storage
 * and returns the URL that can be used to download the file later.
 * 
 * @param[In]  path        The local file path to upload
 * @param[Out] downloadUrl A pointer where the allocated download URL string will be stored. Must be freed by caller.
 * @return int             Returns 0 on success, -1 on error. Errno will be set accordingly.
 */
extern int chef_client_bu_upload(const char* path, char** downloadUrl);

#endif //!__CHEF_CLIENT_STORAGE_BU_H__
