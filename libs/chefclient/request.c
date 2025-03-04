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

#include <errno.h>
#include <chef/client.h>
#include "../private.h"
#include <stdlib.h>
#include <string.h>
#include <vlog.h>

#define DEFAULT_RESPONSE_SIZE 4096

static int __response_writer(char *data, size_t size, size_t nmemb, struct chef_request* request)
{
    size_t length = size * nmemb;

    if (request == NULL || length == 0) {
        return 0;
    }

    // make sure buffer is still large enough, otherwise lets reallocate
    // and we always leave 1 byte for zero termination
    if ((request->response_index + length) >= (request->response_length - 1)) {
        request->response_length = request->response_index + length + 1;
        request->response = realloc(request->response, request->response_length);
    }

    memcpy(request->response + request->response_index, data, length);
    request->response_index += length;
    request->response[request->response_index] = '\0';
    return length;
}

int __init_curl(struct chef_request* request, int https, int authorization)
{
    CURLcode code;
    
    code = curl_easy_setopt(request->curl, CURLOPT_ERRORBUFFER, request->error);
    if (code != CURLE_OK) {
        VLOG_ERROR("chef-client", "__init_curl: failed to set error buffer [%d]\n", code);
        return -1;
    }

    if (chef_trace_requests()) {
        curl_easy_setopt(request->curl, CURLOPT_DEBUGFUNCTION, chef_curl_trace);
        curl_easy_setopt(request->curl, CURLOPT_DEBUGDATA, NULL);
        curl_easy_setopt(request->curl, CURLOPT_VERBOSE, 1L);
    }

    // TODO switch to OpenSSL
    // To get around CA cert issues......
    if (https) {
        curl_easy_setopt(request->curl, CURLOPT_SSL_VERIFYPEER, 0L);
    }
 
    // set the writer function to get the response
    code = curl_easy_setopt(request->curl, CURLOPT_WRITEFUNCTION, (curl_write_callback)__response_writer);
    if (code != CURLE_OK) {
        VLOG_ERROR("chef-client", "__init_curl: failed to set response function [%s]\n", request->error);
        return -1;
    }

    code = curl_easy_setopt(request->curl, CURLOPT_WRITEDATA, request);
    if(code != CURLE_OK) {
        VLOG_ERROR("chef-client", "__init_curl: failed to set write data [%s]\n", request->error);
        return -1;
    }

    chef_set_curl_common_headers((void**)&request->headers, authorization);
    if (request->headers != NULL) {
        code = curl_easy_setopt(request->curl, CURLOPT_HTTPHEADER, request->headers);
        if (code != CURLE_OK) {
            VLOG_ERROR("chef-client", "__init_curl: failed to set http headers [%s]\n", request->error);
            return -1;
        }
    }
    return 0;
}

struct chef_request* chef_request_new(int https, int authorization)
{
    struct chef_request* request = (struct chef_request*)malloc(sizeof(struct chef_request));
    if (request == NULL) {
        return NULL;
    }

    request->curl            = curl_easy_init();
    request->response        = (char*)malloc(DEFAULT_RESPONSE_SIZE);
    request->response_index  = 0;
    request->response_length = DEFAULT_RESPONSE_SIZE;
    request->error           = (char*)malloc(CURL_ERROR_SIZE);
    request->error_length    = CURL_ERROR_SIZE;
    request->headers         = NULL;
    if (request->curl == NULL || request->response == NULL || request->error == NULL) {
        chef_request_delete(request);
        return NULL;
    }

    // reset buffers
    memset(request->response, 0, DEFAULT_RESPONSE_SIZE);
    memset(request->error, 0, CURL_ERROR_SIZE);

    // configure the request
    if (__init_curl(request, https, authorization)) {
        chef_request_delete(request);
        return NULL;
    }
    return request;
}

void chef_request_delete(struct chef_request* request)
{
    if (request == NULL) {
        return; // NOP
    }

    if (request->headers != NULL) {
        curl_slist_free_all(request->headers);
    }

    if (request->curl != NULL) {
        curl_easy_cleanup(request->curl);
    }

    free(request->response);
    free(request->error);
    free(request);
}
