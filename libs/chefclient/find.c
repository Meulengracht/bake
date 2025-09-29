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

#include <chef/client.h>
#include <chef/platform.h>
#include <chef/api/package.h>
#include <curl/curl.h>
#include <errno.h>
#include <jansson.h>
#include "private.h"
#include <string.h>
#include <vlog.h>

extern const char* chefclient_api_base_url(void);

static const char* __get_json_string_safe(json_t* object, const char* key)
{
    json_t* value = json_object_get(object, key);
    if (value != NULL && json_string_value(value) != NULL) {
        return platform_strdup(json_string_value(value));
    }
    return platform_strdup("<not set>");
}

static int __get_find_url(struct chef_find_params* params, char* urlBuffer, size_t bufferSize)
{
    int written = snprintf(urlBuffer, bufferSize - 1, 
        "%s/package/find?search=%s",
        chefclient_api_base_url(),
        params->query
    );
    return written < (bufferSize - 1) ? 0 : -1;
}

static int __parse_package(json_t* root, struct chef_find_result** packageOut)
{
    struct chef_find_result* result;
    json_t*                  platforms;

    // allocate memory for the package
    result = (struct chef_find_result*)malloc(sizeof(struct chef_find_result));
    if (result == NULL) {
        return -1;
    }
    memset(result, 0, sizeof(struct chef_find_result));

    // parse the required members
    result->publisher = __get_json_string_safe(root, "publisher");
    result->package = __get_json_string_safe(root, "name");
    result->summary = __get_json_string_safe(root, "summary");
    result->type = (enum chef_package_type)json_integer_value(json_object_get(root, "type"));
    result->maintainer = __get_json_string_safe(root, "maintainer");
    result->maintainer_email = __get_json_string_safe(root, "maintainer-email");

    *packageOut = result;

    json_decref(root);
    return 0;
}

static int __parse_package_find_response(const char* response, struct chef_find_result*** results, int* count)
{
    json_error_t              error;
    json_t*                   root;
    size_t                    i;
    struct chef_find_result** packages;
    size_t                    packageCount;

    root = json_loads(response, 0, &error);
    if (!root) {
        return -1;
    }

    // root is a list of packages
    packageCount = json_array_size(root);
    if (!packageCount) {
        json_decref(root);
        *results = NULL;
        *count = 0;
        return 0;
    }

    packages = (struct chef_find_result**)calloc(packageCount, sizeof(struct chef_find_result*));
    if (packages == NULL) {
        return -1;
    }

    for (i = 0; i < packageCount; i++) {
        json_t* package = json_array_get(root, i);
        if (__parse_package(package, &packages[i]) != 0) {
            return -1;
        }
    }

    json_decref(root);
    *results = packages;
    *count = (int)packageCount;
    return 0;
}

int chefclient_pack_find(struct chef_find_params* params, struct chef_find_result*** results, int* count)
{
    struct chef_request* request;
    CURLcode             code;
    char                 buffer[256];
    int                  status = -1;
    long                 httpCode;
    VLOG_DEBUG("chef-client", "chefclient_pack_find(query=%s, privileged=%d)\n", params->query, params->privileged);

    request = chef_request_new(CHEF_CLIENT_API_SECURE, params->privileged);
    if (!request) {
        VLOG_ERROR("chef-client", "chefclient_pack_find: failed to create request\n");
        return -1;
    }

    // set the url
    if (__get_find_url(params, buffer, sizeof(buffer)) != 0) {
        VLOG_ERROR("chef-client", "chefclient_pack_find: buffer too small for package info link\n");
        goto cleanup;
    }

    code = curl_easy_setopt(request->curl, CURLOPT_URL, &buffer[0]);
    if (code != CURLE_OK) {
        VLOG_ERROR("chef-client", "chefclient_pack_find: failed to set url [%s]\n", request->error);
        goto cleanup;
    }
    
    code = chef_request_execute(request);
    if (code != CURLE_OK) {
        VLOG_ERROR("chef-client", "chefclient_pack_find: chef_request_execute() failed: %s\n", curl_easy_strerror(code));
    }

    curl_easy_getinfo(request->curl, CURLINFO_RESPONSE_CODE, &httpCode);
    if (httpCode != 200) {
        status = -1;
        
        if (httpCode == 404) {
            VLOG_ERROR("chef-client", "chefclient_pack_find: package not found\n");
            errno = ENOENT;
        }
        else {
            VLOG_ERROR("chef-client", "chefclient_pack_find: http error %ld [%s]\n", httpCode, request->response);
            errno = EIO;
        }
        goto cleanup;
    }

    status = __parse_package_find_response(request->response, results, count);

cleanup:
    chef_request_delete(request);
    return status;
}

void chefclient_pack_find_free(struct chef_find_result** results, int count)
{
    if (results == NULL) {
        return;
    }

    for (int i = 0; i < count; i++) {
        free((void*)results[i]->publisher);
        free((void*)results[i]->package);
        free((void*)results[i]->summary);
        free((void*)results[i]->maintainer);
        free((void*)results[i]->maintainer_email);
        free(results[i]);
    }
    free(results);
}
