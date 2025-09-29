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

#ifndef __LIBCHEF_PRIVATE_H__
#define __LIBCHEF_PRIVATE_H__

#include <curl/curl.h>

struct chef_request {
    CURL*              curl;
    char*              response;
    size_t             response_index;
    size_t             response_length;
    char*              error;
    size_t             error_length;
    struct curl_slist* headers;
};

// Helpers to retrieve the Tenant ID and Client ID for Chef
extern const char* chef_tenant_id(void);
extern const char* chef_client_id(void);
extern int         chef_trace_requests(void);

/**
 * @brief Allocates and initializes a new instance of the request structure
 * 
 * @param[In] https Whether the request should be made over HTTPS
 * @param[In] authorization Whether the request should use an authorization header
 * @return struct chef_request* A malloc'd instance of chef_request
 */
extern struct chef_request* chef_request_new(int https, int authorization);

/**
 * @brief Cleans up any resources allocated by the chef request
 * 
 * @param[In] request The request to clean up
 */
extern void chef_request_delete(struct chef_request* request);

/**
 * @brief This sets only the common headers for a curl request
 * 
 * @param headerlist 
 * @param authorization
 */
extern void chef_set_curl_common_headers(void** headerlist, int authorization);

/**
 * @brief Executes the request and fills in the response and error buffers
 * 
 * @param request The request to execute
 */
extern CURLcode chef_request_execute(struct chef_request* request);

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
