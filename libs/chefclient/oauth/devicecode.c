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

#include <curl/curl.h>
#include <errno.h>
#include "../private.h"
#include "oauth.h"
#include <libplatform.h>
#include <jansson.h>
#include <stdlib.h>
#include <string.h>

struct devicecode_context {
    const char* device_code;
    const char* user_code;
    const char* verification_uri;
    int         expires_in;
    int         interval;
};

static int __get_devicecode_auth_link(char* buffer, size_t maxLength)
{
    int written = snprintf(buffer, maxLength,
        "https://login.microsoftonline.com/%s/oauth2/v2.0/devicecode",
        chef_tenant_id()
    );
    return written < maxLength ? 0 : -1;
}

static int __get_device_auth_body(char* buffer, size_t maxLength)
{
    int written = snprintf(buffer, maxLength,
        "client_id=%s&scope=%s",
        chef_client_id(),
        "user.read%20openid%20profile"
    );
    return written < maxLength ? 0 : -1;
}

static int __get_token_auth_link(char* buffer, size_t maxLength)
{
    int written = snprintf(buffer, maxLength,
        "https://login.microsoftonline.com/%s/oauth2/v2.0/token",
        chef_tenant_id()
    );
    return written < maxLength ? 0 : -1;
}

static int __get_token_auth_body(const char* deviceCode, char* buffer, size_t maxLength)
{
    int written = snprintf(buffer, maxLength,
        "client_id=%s&device_code=%s&grant_type=%s",
        chef_client_id(), deviceCode,
        "urn:ietf:params:oauth:grant-type:device_code"
    );
    return written < maxLength ? 0 : -1;
}

static int __parse_challenge_response(const char* responseBuffer, struct devicecode_context* context)
{
    json_error_t error;
    json_t*      root;
    int          status;

    printf("__parse_challenge_response: %s\n", responseBuffer);

    root = json_loads(responseBuffer, 0, &error);
    if (!root) {
        fprintf(stderr, "__parse_challenge_response: failed to parse json: %s\n", error.text);
        return -1;
    }

    context->user_code = strdup(json_string_value(json_object_get(root, "user_code")));
    context->device_code = strdup(json_string_value(json_object_get(root, "device_code")));
    context->verification_uri = strdup(json_string_value(json_object_get(root, "verification_uri")));
    context->expires_in = json_integer_value(json_object_get(root, "expires_in"));
    context->interval = json_integer_value(json_object_get(root, "interval"));

    json_decref(root);
    return 0;
}

static int __deviceflow_challenge(struct devicecode_context* context)
{
    CURL*    curl;
    CURLcode code;
    size_t   dataIndex = 0;
    char     buffer[256];
    int      status = -1;
    long     httpCode;

    // initialize a curl session
    curl = curl_easy_init();
    if (!curl) {
        fprintf(stderr, "__oauth2_device_flow_start: curl_easy_init() failed\n");
        return -1;
    }
    chef_set_curl_common(curl, NULL, 1, 1, 0);

    // set the url
    if (__get_devicecode_auth_link(buffer, sizeof(buffer)) != 0) {
        fprintf(stderr, "__oauth2_device_flow_start: buffer too small for device code auth link\n");
        goto cleanup;
    }

    code = curl_easy_setopt(curl, CURLOPT_URL, &buffer[0]);
    if (code != CURLE_OK) {
        fprintf(stderr, "__oauth2_device_flow_start: failed to set url [%s]\n", chef_error_buffer());
        goto cleanup;
    }

    code = curl_easy_setopt(curl, CURLOPT_WRITEDATA, &dataIndex);
    if(code != CURLE_OK) {
        fprintf(stderr, "__oauth2_device_flow_start: failed to set write data [%s]\n", chef_error_buffer());
        goto cleanup;
    }

    if (__get_device_auth_body(buffer, sizeof(buffer)) != 0) {
        fprintf(stderr, "__oauth2_device_flow_start: buffer too small for device code auth body\n");
        goto cleanup;
    }

    code = curl_easy_setopt(curl, CURLOPT_POSTFIELDS, &buffer[0]);
    if (code != CURLE_OK) {
        fprintf(stderr, "__oauth2_device_flow_start: failed to set body [%s]\n", chef_error_buffer());
        goto cleanup;
    }

    code = curl_easy_perform(curl);
    if (code != CURLE_OK) {
        fprintf(stderr, "__oauth2_device_flow_start: curl_easy_perform() failed: %s\n", curl_easy_strerror(code));
    }

    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
    if (httpCode != 200) {
        fprintf(stderr, "__oauth2_device_flow_start: http error %ld [%s]\n", httpCode, chef_response_buffer());
        status = -1;
        errno = EIO;
        goto cleanup;
    }

    status = __parse_challenge_response(chef_response_buffer(), context);

cleanup:
    curl_easy_cleanup(curl);
    return status;
}

static int __parse_token_response(const char* responseBuffer, struct token_context* context)
{
    json_error_t error;
    json_t*      root;
    int          status;

    printf("__parse_token_response: %s\n", responseBuffer);

    root = json_loads(responseBuffer, 0, &error);
    if (!root) {
        fprintf(stderr, "__parse_token_response: failed to parse json: %s\n", error.text);
        return -1;
    }

    context->access_token = strdup(json_string_value(json_object_get(root, "access_token")));
    context->refresh_token = strdup(json_string_value(json_object_get(root, "refresh_token")));
    context->expires_in = json_integer_value(json_object_get(root, "expires_in"));

    json_decref(root);
    return 0;
}

static void __parse_token_error_response(const char* responseBuffer)
{
    json_error_t error;
    json_t*      root;
    int          status;
    const char*  statusText;

    printf("__parse_token_error_response: %s\n", responseBuffer);

    root = json_loads(responseBuffer, 0, &error);
    if (!root) {
        fprintf(stderr, "__parse_token_error_response: failed to parse json: %s\n", error.text);
        return;
    }

    statusText = json_string_value(json_object_get(root, "access_token"));
    if (strncmp(statusText, "authorization_pending", 21) == 0) {
        errno = EAGAIN;
    }
    else if (strncmp(statusText, "slow_down", 9) == 0) {
        errno = EBUSY;
    }
    else {
        errno = EPIPE;
    }
    json_decref(root);
}

static int __deviceflow_get_token(struct devicecode_context* deviceContext, struct token_context* tokenContext)
{
    CURL*    curl;
    CURLcode code;
    size_t   dataIndex = 0;
    char     buffer[512];
    int      status = -1;
    long     httpCode;

    // initialize a curl session
    curl = curl_easy_init();
    if (!curl) {
        fprintf(stderr, "__deviceflow_get_token: curl_easy_init() failed\n");
        return -1;
    }
    chef_set_curl_common(curl, NULL, 1, 1, 0);

    // set the url
    if (__get_token_auth_link(buffer, sizeof(buffer)) != 0) {
        fprintf(stderr, "__deviceflow_get_token: buffer too small for token auth link\n");
        goto cleanup;
    }

    code = curl_easy_setopt(curl, CURLOPT_URL, &buffer[0]);
    if (code != CURLE_OK) {
        fprintf(stderr, "__deviceflow_get_token: failed to set url [%s]\n", chef_error_buffer());
        goto cleanup;
    }

    code = curl_easy_setopt(curl, CURLOPT_WRITEDATA, &dataIndex);
    if(code != CURLE_OK) {
        fprintf(stderr, "__deviceflow_get_token: failed to set write data [%s]\n", chef_error_buffer());
        goto cleanup;
    }

    if (__get_token_auth_body(deviceContext->device_code, buffer, sizeof(buffer)) != 0) {
        fprintf(stderr, "__deviceflow_get_token: buffer too small for token auth body\n");
        goto cleanup;
    }

    code = curl_easy_setopt(curl, CURLOPT_POSTFIELDS, &buffer[0]);
    if (code != CURLE_OK) {
        fprintf(stderr, "__deviceflow_get_token: failed to set body [%s]\n", chef_error_buffer());
        goto cleanup;
    }

    code = curl_easy_perform(curl);
    if (code != CURLE_OK) {
        fprintf(stderr, "__deviceflow_get_token: curl_easy_perform() failed: %s\n", curl_easy_strerror(code));
    }

    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
    if (httpCode == 200 && code != CURLE_ABORTED_BY_CALLBACK) {
        status = __parse_token_response(chef_response_buffer(), tokenContext);
    }
    else {
        // this will set errno appropriately
        __parse_token_error_response(chef_response_buffer());
        status = -1;
    }

cleanup:
    curl_easy_cleanup(curl);
    return status;
}

static int __deviceflow_poll(struct devicecode_context* deviceContext, struct token_context* tokenContext)
{
    int expires_in = deviceContext->expires_in;

    while (expires_in > 0) {
        if (deviceContext->interval > 0) {
            platform_sleep(deviceContext->interval * 1000);
            expires_in -= deviceContext->interval;
        }

        if (__deviceflow_get_token(deviceContext, tokenContext) == 0) {
            return 0;
        }

        if (errno == EBUSY) {
            // slow down, increase interval
            deviceContext->interval += 5;
        }
        else if (errno != EAGAIN) {
            break;
        }
    }
    return -1;
}

int oauth_deviceflow_start(struct token_context* tokenContext)
{
    struct devicecode_context* deviceContext;
    int                        status;

    deviceContext = calloc(1, sizeof(struct devicecode_context));
    if (!deviceContext) {
        fprintf(stderr, "oauth_deviceflow_start: failed to allocate device context\n");
        return -1;
    }

    status = __deviceflow_challenge(deviceContext);
    if (status != 0) {
        free(deviceContext);
        fprintf(stderr, "oauth_deviceflow_start: failed to get device code\n");
        return status;
    }

    printf("To sign in, use a web browser to open the page %s and enter the code %s to authenticate.\n", 
        deviceContext->verification_uri, deviceContext->user_code);

    status = __deviceflow_poll(deviceContext, tokenContext);
    if (status != 0) {
        fprintf(stderr, "oauth_deviceflow_start: failed to retrieve access token\n");
    }

    free(deviceContext);
    return 0;
}
