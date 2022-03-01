/**
 * Copyright 2022, Philip Meulengracht
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

#ifndef __LIBCHEF_PRIVATE_H__
#define __LIBCHEF_PRIVATE_H__

#include <curl/curl.h>

#define MAX_RESPONSE_SIZE 4096

/**
 * @brief 
 * 
 * @return const char* 
 */
extern const char* chef_tenant_id(void);


/**
 * @brief 
 * 
 * @return const char* 
 */
extern const char* chef_client_id(void);

/**
 * @brief 
 * 
 * @return char* 
 */
extern char* chef_response_buffer(void);

/**
 * @brief 
 * 
 * @return char* 
 */
extern char* chef_error_buffer(void);

/**
 * @brief 
 * 
 * @param curl 
 * @param response
 * @param secure
 * @param authorization
 */
extern void chef_set_curl_common(void* curl, void** headerlist, int response, int secure, int authorization);

/**
 * @brief 
 * 
 * @param handle 
 * @param type 
 * @param data 
 * @param size 
 * @param userp 
 * @return int 
 */
extern int chef_curl_trace(CURL *handle, curl_infotype type, char *data, size_t size, void *userp);

#endif //!__LIBCHEF_PRIVATE_H__
