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
#include <regex/regex.h>
#include <stdlib.h>
#include <string.h>

struct devicecode_context {
    const char* device_code;
    const char* user_code;
    const char* verification_uri;
    int         expires_in;
    int         interval;
};

struct token_context {
    const char* access_token;
    const char* refresh_token;
    int         expires_in;
};

static int __response_writer(char *data, size_t size, size_t nmemb, size_t* dataIndex)
{
    if (dataIndex == NULL) {
        return 0;
    }

    memcpy(chef_response_buffer() + *dataIndex, data, size * nmemb);
    return size * nmemb;
}

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

static int __get_usercode(const char* response, struct devicecode_context* context)
{
    regex_t    regex;
    regmatch_t matches[2];
    int        status;
    int        codeLength;

    status = regcomp(&regex, "\"user_code\":\"([a-zA-Z0-9]+)\"", REG_EXTENDED);
    if (status != 0) {
        fprintf(stderr, "__get_usercode: failed to compile regex: %i\n", status);
        return status;
    }

    status = regexec(&regex, response, 2, matches, 0);
    if (status != 0) {
        fprintf(stderr, "__get_usercode: failed to match regex: %i\n", status);
        goto cleanup;
    }

    codeLength = matches[1].rm_eo - matches[1].rm_so;
    if (codeLength > 0) {
        context->user_code = strndup(response + matches[1].rm_so, codeLength);
    }

cleanup:
    regfree(&regex);
    return status;
}

static int __get_devicecode(const char* response, struct devicecode_context* context)
{
    regex_t    regex;
    regmatch_t matches[2];
    int        status;
    int        codeLength;

    status = regcomp(&regex, "\"device_code\":\"([a-zA-Z0-9_\\-]+)\"", REG_EXTENDED);
    if (status != 0) {
        fprintf(stderr, "__get_devicecode: failed to compile regex: %i\n", status);
        return status;
    }

    status = regexec(&regex, response, 2, matches, 0);
    if (status != 0) {
        fprintf(stderr, "__get_devicecode: failed to match regex: %i\n", status);
        goto cleanup;
    }

    codeLength = matches[1].rm_eo - matches[1].rm_so;
    if (codeLength > 0) {
        context->device_code = strndup(response + matches[1].rm_so, codeLength);
    }

cleanup:
    regfree(&regex);
    return status;
}

static int __get_verification_url(const char* response, struct devicecode_context* context)
{
    regex_t    regex;
    regmatch_t matches[2];
    int        status;
    int        urlLength;

    status = regcomp(&regex, "\"verification_uri\":\"([a-zA-Z0-9\\-_:\\/\\.]+)\"", REG_EXTENDED);
    if (status != 0) {
        return status;
    }

    status = regexec(&regex, response, 2, matches, 0);
    if (status != 0) {
        goto cleanup;
    }

    urlLength = matches[1].rm_eo - matches[1].rm_so;
    if (urlLength > 0) {
        context->verification_uri = strndup(response + matches[1].rm_so, urlLength);
    }

cleanup:
    regfree(&regex);
    return status;
}

static int __get_expires_in(const char* response, struct devicecode_context* context)
{
    regex_t    regex;
    regmatch_t matches[2];
    int        status;
    int        valueLength;

    status = regcomp(&regex, "\"expires_in\":([0-9]+)", REG_EXTENDED);
    if (status != 0) {
        return status;
    }

    status = regexec(&regex, response, 2, matches, 0);
    if (status != 0) {
        goto cleanup;
    }

    valueLength = matches[1].rm_eo - matches[1].rm_so;
    if (valueLength > 0) {
        context->expires_in = atoi(response + matches[1].rm_so);
    }

cleanup:
    regfree(&regex);
    return status;
}

static int __get_interval(const char* response, struct devicecode_context* context)
{
    regex_t    regex;
    regmatch_t matches[2];
    int        status;
    int        valueLength;

    status = regcomp(&regex, "\"interval\":([0-9]+)", REG_EXTENDED);
    if (status != 0) {
        return status;
    }

    status = regexec(&regex, response, 2, matches, 0);
    if (status != 0) {
        goto cleanup;
    }

    valueLength = matches[1].rm_eo - matches[1].rm_so;
    if (valueLength > 0) {
        context->interval = atoi(response + matches[1].rm_so);
    }

cleanup:
    regfree(&regex);
    return status;
}

static int __parse_challenge_response(const char* responseBuffer, struct devicecode_context* context)
{
    int status;

    status = __get_usercode(responseBuffer, context);
    if (status != 0) {
        fprintf(stderr, "__oauth2_device_flow_start: failed to parse usercode: %s\n", responseBuffer);
        return -1;
    }
    
    status = __get_devicecode(responseBuffer, context);
    if (status != 0) {
        fprintf(stderr, "__oauth2_device_flow_start: failed to parse devicecode: %s\n", responseBuffer);
        return -1;
    }
    
    status = __get_verification_url(responseBuffer, context);
    if (status != 0) {
        fprintf(stderr, "__oauth2_device_flow_start: failed to parse verification url: %s\n", responseBuffer);
        return -1;
    }

    status = __get_expires_in(responseBuffer, context);
    if (status != 0) {
        fprintf(stderr, "__oauth2_device_flow_start: failed to parse expiration value: %s\n", responseBuffer);
        return -1;
    }
    
    status = __get_interval(responseBuffer, context);
    if (status != 0) {
        fprintf(stderr, "__oauth2_device_flow_start: failed to parse interval: %s\n", responseBuffer);
        return -1;
    }
    
    return 0;
}

static int __deviceflow_challenge(struct devicecode_context* context)
{
    CURL*    curl;
    CURLcode code;
    size_t   dataIndex = 0;
    char     buffer[256];
    int      status = -1;

    // initialize a curl session
    curl = curl_easy_init();
    if (!curl) {
        fprintf(stderr, "__oauth2_device_flow_start: curl_easy_init() failed\n");
        return -1;
    }

    code = curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, chef_error_buffer());
    if (code != CURLE_OK) {
        fprintf(stderr, "__oauth2_device_flow_start: failed to set error buffer [%d]\n", code);
        goto cleanup;
    }

    if (chef_curl_trace_enabled()) {
        curl_easy_setopt(curl, CURLOPT_DEBUGFUNCTION, chef_curl_trace);
        curl_easy_setopt(curl, CURLOPT_DEBUGDATA, NULL);
        curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
    }
 
    // To get around CA cert issues......
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
 
    // set the url
    __get_devicecode_auth_link(buffer, sizeof(buffer));
    code = curl_easy_setopt(curl, CURLOPT_URL, &buffer[0]);
    if (code != CURLE_OK) {
        fprintf(stderr, "__oauth2_device_flow_start: failed to set url [%s]\n", chef_error_buffer());
        goto cleanup;
    }

    // set the writer function to get the response
    code = curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, __response_writer);
    if (code != CURLE_OK) {
        fprintf(stderr, "__oauth2_device_flow_start: failed to set writer [%s]\n", chef_error_buffer());
        goto cleanup;
    }
    
    code = curl_easy_setopt(curl, CURLOPT_WRITEDATA, &dataIndex);
    if(code != CURLE_OK) {
        fprintf(stderr, "__oauth2_device_flow_start: failed to set write data [%s]\n", chef_error_buffer());
        goto cleanup;
    }

    __get_device_auth_body(buffer, sizeof(buffer));
    code = curl_easy_setopt(curl, CURLOPT_POSTFIELDS, &buffer[0]);
    if (code != CURLE_OK) {
        fprintf(stderr, "__oauth2_device_flow_start: failed to set body [%s]\n", chef_error_buffer());
        goto cleanup;
    }

    code = curl_easy_perform(curl);
    if (code != CURLE_OK) {
        fprintf(stderr, "__oauth2_device_flow_start: curl_easy_perform() failed: %s\n", curl_easy_strerror(code));
    }

    status = __parse_challenge_response(chef_response_buffer(), context);

cleanup:
    curl_easy_cleanup(curl);
    return status;
}

static int __parse_token_response(const char* responseBuffer, struct token_context* context)
{
    return 0;
}

static int __deviceflow_get_token(struct devicecode_context* deviceContext, struct token_context* tokenContext)
{
    CURL*    curl;
    CURLcode code;
    size_t   dataIndex = 0;
    char     buffer[256];
    int      status = -1;

    // initialize a curl session
    curl = curl_easy_init();
    if (!curl) {
        fprintf(stderr, "__deviceflow_get_token: curl_easy_init() failed\n");
        return -1;
    }

    code = curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, chef_error_buffer());
    if (code != CURLE_OK) {
        fprintf(stderr, "__deviceflow_get_token: failed to set error buffer [%d]\n", code);
        goto cleanup;
    }

    if (chef_curl_trace_enabled()) {
        curl_easy_setopt(curl, CURLOPT_DEBUGFUNCTION, chef_curl_trace);
        curl_easy_setopt(curl, CURLOPT_DEBUGDATA, NULL);
        curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
    }
 
    // To get around CA cert issues......
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
 
    // set the url
    __get_token_auth_link(buffer, sizeof(buffer));
    code = curl_easy_setopt(curl, CURLOPT_URL, &buffer[0]);
    if (code != CURLE_OK) {
        fprintf(stderr, "__deviceflow_get_token: failed to set url [%s]\n", chef_error_buffer());
        goto cleanup;
    }

    // set the writer function to get the response
    code = curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, __response_writer);
    if (code != CURLE_OK) {
        fprintf(stderr, "__deviceflow_get_token: failed to set writer [%s]\n", chef_error_buffer());
        goto cleanup;
    }
    
    code = curl_easy_setopt(curl, CURLOPT_WRITEDATA, &dataIndex);
    if(code != CURLE_OK) {
        fprintf(stderr, "__deviceflow_get_token: failed to set write data [%s]\n", chef_error_buffer());
        goto cleanup;
    }

    __get_token_auth_body(deviceContext->device_code, buffer, sizeof(buffer));
    code = curl_easy_setopt(curl, CURLOPT_POSTFIELDS, &buffer[0]);
    if (code != CURLE_OK) {
        fprintf(stderr, "__deviceflow_get_token: failed to set body [%s]\n", chef_error_buffer());
        goto cleanup;
    }

    code = curl_easy_perform(curl);
    if (code != CURLE_OK) {
        fprintf(stderr, "__deviceflow_get_token: curl_easy_perform() failed: %s\n", curl_easy_strerror(code));
    }

    status = __parse_token_response(chef_response_buffer(), tokenContext);

cleanup:
    curl_easy_cleanup(curl);
    return status;
}

static int __deviceflow_poll(struct devicecode_context* deviceContext, struct token_context* tokenContext)
{
    int expires_in = deviceContext->expires_in;
    while (expires_in > 0) {
        if (deviceContext->interval > 0) {
            sleep(deviceContext->interval);
            expires_in -= deviceContext->interval;
        }

        if (__deviceflow_get_token(deviceContext, tokenContext) == 0) {
            return 0;
        }
    }
    return -1;
}

int oauth_deviceflow_start(char* accessToken, size_t accessTokenLength, char* refreshToken, size_t refreshTokenLength)
{
    struct devicecode_context* deviceContext;
    struct token_context*      tokenContext;
    int                        status;

    deviceContext = calloc(1, sizeof(struct devicecode_context));
    if (!deviceContext) {
        fprintf(stderr, "oauth_deviceflow_start: failed to allocate device context\n");
        return -1;
    }

    status = __deviceflow_challenge(deviceContext);
    if (status != 0) {
        fprintf(stderr, "oauth_deviceflow_start: failed to get device code\n");
        return status;
    }

    printf("To sign in, use a web browser to open the page %s and enter the code %s to authenticate.\n", 
        deviceContext->verification_uri, deviceContext->user_code);

    //status = __deviceflow_poll(deviceContext, tokenContext);
    //if (status != 0) {
    //    fprintf(stderr, "oauth_deviceflow_start: failed to retrieve access token\n");
    //    return status;
    //}

    return 0;
}
