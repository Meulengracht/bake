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
#include <chef/api/package_settings.h>
#include <curl/curl.h>
#include <errno.h>
#include <jansson.h>
#include "private.h"
#include <string.h>
#include <vlog.h>

struct chef_package_settings {
    const char* package;
    int discoverable;
};

static int __get_settings_url(char* urlBuffer, size_t bufferSize)
{
    int written = snprintf(urlBuffer, bufferSize - 1, 
        "https://chef-api.azurewebsites.net/api/pack/settings"
    );
    return written < (bufferSize - 1) ? 0 : -1;
}

static int __get_settings_query_url(struct chef_settings_params* params, char* urlBuffer, size_t bufferSize)
{
    int written = snprintf(urlBuffer, bufferSize - 1, 
        "https://chef-api.azurewebsites.net/api/pack/settings?name=%s",
        params->package
    );
    return written < (bufferSize - 1) ? 0 : -1;
}

static json_t* __serialize_pack_settings(struct chef_package_settings* settings)
{
    json_t* json = json_object();
    if (json == NULL) {
        errno = ENOMEM;
        return NULL;
    }

    // never need to serialize publisher
    
    // serialize package name
    json_object_set_new(json, "name", json_string(settings->package));

    // serialize settings
    json_object_set_new(json, "discoverable", json_boolean(settings->discoverable));

    return json;
}

static int __parse_pack_settings(const char* response, struct chef_package_settings** settingsOut)
{
    struct chef_package_settings* settings;
    json_error_t                  error;
    json_t*                       root;
    json_t*                       name;
    json_t*                       discoverable;

    root = json_loads(response, 0, &error);
    if (!root) {
        return -1;
    }

    settings = chef_package_settings_new();
    if (settings == NULL) {
        return -1;
    }

    // read name
    name = json_object_get(root, "name");
    if (name) {
        settings->package = platform_strdup(json_string_value(name));
    }

    discoverable = json_object_get(root, "discoverable");
    if (discoverable) {
        settings->discoverable = json_boolean_value(discoverable);
    }

    *settingsOut = settings;

    json_decref(root);
    return 0;
}

int __get_settings(struct chef_settings_params* params, struct chef_package_settings** settingsOut)
{
    struct chef_request* request;
    CURLcode             code;
    char                 buffer[256];
    int                  status = -1;
    long                 httpCode;

    request = chef_request_new(1, 1);
    if (!request) {
        VLOG_ERROR("chef-client", "__get_settings: failed to create request\n");
        return -1;
    }

    // set the url
    if (__get_settings_query_url(params, buffer, sizeof(buffer)) != 0) {
        VLOG_ERROR("chef-client", "__get_account: buffer too small for account link\n");
        goto cleanup;
    }

    code = curl_easy_setopt(request->curl, CURLOPT_URL, &buffer[0]);
    if (code != CURLE_OK) {
        VLOG_ERROR("chef-client", "__get_account: failed to set url [%s]\n", request->error);
        goto cleanup;
    }

    code = curl_easy_perform(request->curl);
    if (code != CURLE_OK) {
        VLOG_ERROR("chef-client", "__get_account: curl_easy_perform() failed: %s\n", curl_easy_strerror(code));
    }

    curl_easy_getinfo(request->curl, CURLINFO_RESPONSE_CODE, &httpCode);
    if (httpCode != 200) {
        if (httpCode == 404) {
            status = -ENOENT;
        }
        else if (httpCode == 302) {
            status = -EACCES;
        }
        else {
            VLOG_ERROR("chef-client", "__get_account: http error %ld\n", httpCode);
            status = -EIO;
        }
        goto cleanup;
    }

    status = __parse_pack_settings(request->response, settingsOut);

cleanup:
    chef_request_delete(request);
    return status;
}

static int __update_settings(json_t* json, struct chef_package_settings** settingsOut)
{
    struct chef_request* request;
    CURLcode             code;
    char*                body   = NULL;
    int                  status = -1;
    char                 buffer[256];
    long                 httpCode;

    request = chef_request_new(1, 1);
    if (!request) {
        VLOG_ERROR("chef-client", "__update_settings: failed to create request\n");
        return -1;
    }

    // set the url
    if (__get_settings_url(buffer, sizeof(buffer)) != 0) {
        VLOG_ERROR("chef-client", "__update_account: buffer too small for account link\n");
        goto cleanup;
    }

    code = curl_easy_setopt(request->curl, CURLOPT_URL, &buffer[0]);
    if (code != CURLE_OK) {
        VLOG_ERROR("chef-client", "__update_account: failed to set url [%s]\n", request->error);
        goto cleanup;
    }

    body = json_dumps(json, 0);
    code = curl_easy_setopt(request->curl, CURLOPT_POSTFIELDS, body);
    if (code != CURLE_OK) {
        VLOG_ERROR("chef-client", "__update_account: failed to set body [%s]\n", request->error);
        goto cleanup;
    }

    code = curl_easy_perform(request->curl);
    if (code != CURLE_OK) {
        VLOG_ERROR("chef-client", "__update_account: curl_easy_perform() failed: %s\n", curl_easy_strerror(code));
    }

    curl_easy_getinfo(request->curl, CURLINFO_RESPONSE_CODE, &httpCode);
    if (httpCode < 200 || httpCode >= 300) {
        status = -1;
        if (httpCode == 302) {
            status = -EACCES;
        }
        else {
            VLOG_ERROR("chef-client", "__update_account: http error %ld [%s]\n", httpCode, request->response);
            status = -EIO;
        }
        goto cleanup;
    } else {
        status = 0;
    }

    if (settingsOut != NULL) {
        status = __parse_pack_settings(request->response, settingsOut);
    }

cleanup:
    free(body);
    chef_request_delete(request);
    return status;
}

int chefclient_pack_settings_get(struct chef_settings_params* params, struct chef_package_settings** settingsOut)
{
    if (params == NULL || settingsOut == NULL) {
        errno = EINVAL;
        return -1;
    }

    return __get_settings(params, settingsOut);
}

int chefclient_pack_settings_update(struct chef_package_settings* settings)
{
    json_t* json;
    int     status;

    if (settings == NULL) {
        errno = EINVAL;
        return -1;
    }

    json = __serialize_pack_settings(settings);
    if (json == NULL) {
        return -1;
    }

    status = __update_settings(json, NULL);

    json_decref(json);
    return status;
}

struct chef_package_settings* chef_package_settings_new(void)
{
    struct chef_package_settings* settings = (struct chef_package_settings*)malloc(sizeof(struct chef_package_settings));
    if (settings == NULL) {
        errno = ENOMEM;
        return NULL;
    }

    memset(settings, 0, sizeof(struct chef_package_settings));
    return settings;
}

void chef_package_settings_delete(struct chef_package_settings* settings)
{
    if (settings == NULL) {
        errno = EINVAL;
        return;
    }

    free((void*)settings->package);
    free(settings);
}

const char* chef_package_settings_get_package(struct chef_package_settings* settings)
{
    if (settings == NULL) {
        errno = EINVAL;
        return NULL;
    }

    return settings->package;
}

int chef_package_settings_get_discoverable(struct chef_package_settings* settings)
{
    if (settings == NULL) {
        errno = EINVAL;
        return -1;
    }

    return settings->discoverable;
}

void chef_package_settings_set_discoverable(struct chef_package_settings* settings, int discoverable)
{
    if (settings == NULL) {
        errno = EINVAL;
        return;
    }

    settings->discoverable = discoverable;
}
