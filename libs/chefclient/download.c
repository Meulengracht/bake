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

#include <errno.h>
#include <chef/client.h>
#include <curl/curl.h>
#include <jansson.h>
#include "private.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct pack_response {
    const char* token;
    const char* url;
};

static int __get_download_url(struct chef_download_params* params, char* urlBuffer, size_t bufferSize)
{
    // todo specific version support
    int written = snprintf(urlBuffer, bufferSize - 1, 
        "https://chef-api.azurewebsites.net/api/pack/download?publisher=%s&name=%s&channel=%s",
        params->publisher, params->package, params->channel
    );
    urlBuffer[written] = '\0';
    return written == bufferSize - 1 ? -1 : 0;
}

static int __parse_pack_response(const char* response, struct pack_response* packResponse)
{
    json_error_t error;
    json_t*      root;

    printf("__parse_pack_response: %s\n", response);

    root = json_loads(response, 0, &error);
    if (!root) {
        return -1;
    }

    packResponse->token = strdup(json_string_value(json_object_get(root, "sas-token")));
    packResponse->url = strdup(json_string_value(json_object_get(root, "blob-url")));
    json_decref(root);

    return 0;
}

int __download_request(struct chef_download_params* params, struct pack_response* packResponse)
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
        fprintf(stderr, "__download_request: curl_easy_init() failed\n");
        return -1;
    }
    chef_set_curl_common(curl, NULL, 1, 1, 0);

    // set the url
    if (__get_download_url(params, buffer, sizeof(buffer)) != 0) {
        fprintf(stderr, "__download_request: buffer too small for package download link\n");
        goto cleanup;
    }

    code = curl_easy_setopt(curl, CURLOPT_URL, &buffer[0]);
    if (code != CURLE_OK) {
        fprintf(stderr, "__download_request: failed to set url [%s]\n", chef_error_buffer());
        goto cleanup;
    }

    code = curl_easy_setopt(curl, CURLOPT_WRITEDATA, &dataIndex);
    if(code != CURLE_OK) {
        fprintf(stderr, "__download_request: failed to set write data [%s]\n", chef_error_buffer());
        goto cleanup;
    }

    code = curl_easy_perform(curl);
    if (code != CURLE_OK) {
        fprintf(stderr, "__download_request: curl_easy_perform() failed: %s\n", curl_easy_strerror(code));
    }

    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
    if (httpCode != 200) {
        status = -1;
        
        if (httpCode == 404) {
            fprintf(stderr, "__download_request: package not found\n");
            errno = ENOENT;
        }
        else {
            fprintf(stderr, "__download_request: http error %ld [%s]\n", httpCode, chef_response_buffer());
            errno = EIO;
        }
        goto cleanup;
    }

    status = __parse_pack_response(chef_response_buffer(), packResponse);

cleanup:
    curl_easy_cleanup(curl);
    return status;
}

static int __download_file(const char* filePath, struct pack_response* context)
{
    FILE*              file;
    struct curl_slist* headers = NULL;
    int                status  = -1;
    CURL*              curl;
    CURLcode           code;

    // initialize a curl session
    curl = curl_easy_init();
    if (!curl) {
        fprintf(stderr, "__download_file: curl_easy_init() failed\n");
        return -1;
    }
    chef_set_curl_common(curl, NULL, 0, 1, 1);

    // initialize the output file
    file = fopen(filePath, "wb");
    if (!file) {
        fprintf(stderr, "__download_file: failed to open file [%s]\n", strerror(errno));
        goto cleanup;
    }

    // set required ms headers
    headers = curl_slist_append(headers, "x-ms-blob-type:BlockBlob");
    headers = curl_slist_append(headers, "x-ms-blob-content-type:application/octet-stream");

    code = curl_easy_setopt(curl, CURLOPT_WRITEDATA, file);
    if (code != CURLE_OK) {
        fprintf(stderr, "__download_file: failed to set upload data [%s]\n", chef_error_buffer());
        goto cleanup;
    }

    code = curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    if (code != CURLE_OK) {
        fprintf(stderr, "__download_file: failed to set http headers [%s]\n", chef_error_buffer());
        goto cleanup;
    }

    code = curl_easy_setopt(curl, CURLOPT_URL, context->url);
    if (code != CURLE_OK) {
        fprintf(stderr, "__download_file: failed to set url [%s]\n", chef_error_buffer());
        goto cleanup;
    }

    code = curl_easy_perform(curl);
    if (code != CURLE_OK) {
        fprintf(stderr, "__download_file: curl_easy_perform() failed: %s\n", chef_error_buffer());
    }

cleanup:
    if (file) {
        fclose(file);
    }
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return status;
}

int chefclient_pack_download(struct chef_download_params* params, const char* path)
{
    struct pack_response packResponse = { 0 };
    int                  status;

    status = __download_request(params, &packResponse);
    if (status != 0) {
        return status;
    }

    status = __download_file(path, &packResponse);
    if (status != 0) {
        return status;
    }
    return 0;
}
