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

#ifndef __CHEF_CLIENT_STORAGE_H__
#define __CHEF_CLIENT_STORAGE_H__

/**
 * @brief Downloads a file from a URL to a local path.
 * 
 * This function performs a generic download operation, fetching content from
 * the specified URL and saving it to the local filesystem at the given path.
 * 
 * @param[In] url  The URL to download from
 * @param[In] path The local file path where the downloaded content should be saved
 * @return int     Returns 0 on success, -1 on error. Errno will be set accordingly.
 */
extern int chef_client_gen_download(const char* url, const char* path);

#endif //!__CHEF_CLIENT_STORAGE_H__
