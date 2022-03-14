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
#include <chef/client.h>
#include <errno.h>
#include "oauth/oauth.h"
#include "private.h"
#include <libplatform.h>
#include <jansson.h>
#include <stdlib.h>
#include <string.h>
#include <wolfssl/ssl.h>

static const char* g_mollenosTenantId   = "d8acf75d-9820-4522-a25b-ad672acc5fdd";
static const char* g_chefClientId       = "17985824-571b-4bdf-b291-c25b2ff14837";
static char*       g_curlresponseBuffer = NULL;
static char*       g_curlErrorBuffer    = NULL; // CURL_ERROR_SIZE
static int         g_curlTrace          = 0;

// settings object
json_t* g_chefSettings = NULL;

static void __initialize_settings(void)
{
    g_chefSettings = json_object();
}

static int __load_settings(void)
{
    json_error_t error;
    char*        path;
    int          status;

    if (g_chefSettings != NULL) {
        return 0;
    }

    path = malloc(PATH_MAX);
    if (path == NULL) {
        errno = ENOMEM;
        return -1;
    }

    status = platform_getuserdir(path, PATH_MAX);
    if (status != 0) {
        free(path);
        return -1;
    }

    // append filename
    strcat(path, "/.chef/settings.json");

    g_chefSettings = json_load_file(path, 0, &error);
    if (g_chefSettings == NULL) {
        // handle if file not found
        if (json_error_code(&error) == json_error_cannot_open_file) {
            __initialize_settings();
        }
        else {
            free(path);
            return -1;
        }
    }
    free(path);
    return 0;
}

static int __save_settings(void)
{
    char* path;
    int   status;

    if (g_chefSettings == NULL) {
        return 0;
    }

    path = malloc(PATH_MAX);
    if (path == NULL) {
        errno = ENOMEM;
        return -1;
    }

    status = platform_getuserdir(path, PATH_MAX);
    if (status != 0) {
        fprintf(stderr, "__save_settings: failed to get user directory: %s\n", strerror(errno));
        free(path);
        return -1;
    }

    // append directory, and make sure directory exists
    strcat(path, "/.chef");
    status = platform_mkdir(path);
    if (status != 0) {
        fprintf(stderr, "__save_settings: failed to create directory: %s\n", strerror(errno));
        free(path);
        return -1;
    }

    // append filename
    strcat(path, "/settings.json");

    status = json_dump_file(g_chefSettings, path, JSON_INDENT(2));
    if (status != 0) {
        free(path);
        return -1;
    }
    free(path);
    return 0;
}

int chefclient_initialize(void)
{
    int status;

    g_curlresponseBuffer = (char*)malloc(MAX_RESPONSE_SIZE);
    if (g_curlresponseBuffer == NULL) {
        fprintf(stderr, "chefclient_initialize: failed to allocate response buffer\n");
        return -1;
    }

    g_curlErrorBuffer = (char*)malloc(CURL_ERROR_SIZE);
    if (g_curlErrorBuffer == NULL) {
        fprintf(stderr, "chefclient_initialize: failed to allocate error buffer\n");
        return -1;
    }

    // required on windows
    curl_global_init(CURL_GLOBAL_ALL);

    // load settings
    status = __load_settings();
    if (status != 0) {
        fprintf(stderr, "chefclient_initialize: failed to load settings\n");
        return -1;
    }
    return 0;
}

void chefclient_cleanup(void)
{
    // save settings
    if (__save_settings() != 0) {
        fprintf(stderr, "chefclient_cleanup: failed to save settings\n");
    }

    // required on windows
    curl_global_cleanup();

    if (g_curlresponseBuffer) {
        free(g_curlresponseBuffer);
        g_curlresponseBuffer = NULL;
    }

    if (g_curlErrorBuffer) {
        free(g_curlErrorBuffer);
        g_curlErrorBuffer = NULL;
    }
}

const char* chef_tenant_id(void)
{
    return g_mollenosTenantId;
}

const char* chef_client_id(void)
{
    return g_chefClientId;
}

char* chef_response_buffer(void)
{
    return g_curlresponseBuffer;
}

char* chef_error_buffer(void)
{
    return g_curlErrorBuffer;
}

static int __response_writer(char *data, size_t size, size_t nmemb, size_t* dataIndex)
{
    if (dataIndex == NULL) {
        return 0;
    }

    memcpy(chef_response_buffer() + *dataIndex, data, size * nmemb);
    *dataIndex += size * nmemb;
    return size * nmemb;
}

void chef_set_curl_common(void* curl, void** headerlist, int response, int secure, int authorization)
{
    CURLcode code;
    
    code = curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, g_curlErrorBuffer);
    if (code != CURLE_OK) {
        fprintf(stderr, "chef_set_curl_common: failed to set error buffer [%d]\n", code);
    }

    if (g_curlTrace) {
        curl_easy_setopt(curl, CURLOPT_DEBUGFUNCTION, chef_curl_trace);
        curl_easy_setopt(curl, CURLOPT_DEBUGDATA, NULL);
        curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
    }

    // To get around CA cert issues......
    if (secure) {
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    }
 
    // set the writer function to get the response
    if (response) {
        memset(g_curlresponseBuffer, 0, MAX_RESPONSE_SIZE);
        code = curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, __response_writer);
        if (code != CURLE_OK) {
            fprintf(stderr, "chef_set_curl_common: failed to set writer [%s]\n", g_curlErrorBuffer);
        }
    }

    if (authorization) {
        if (headerlist == NULL) {
            fprintf(stderr, "chef_set_curl_common: auth requested but headerlist is NULL\n");
            return;
        }
        oauth_set_authentication(headerlist);
    }

    if (headerlist && *headerlist) {
        code = curl_easy_setopt(curl, CURLOPT_HTTPHEADER, (struct curl_slist*)(*headerlist));
        if (code != CURLE_OK) {
            fprintf(stderr, "chef_set_curl_common: failed to set http headers [%s]\n", g_curlErrorBuffer);
        }
    }
}
