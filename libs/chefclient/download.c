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

struct download_context {
    const char* publisher;
    const char* package;
    size_t      bytes_downloaded;
    size_t      bytes_total;
};

static void __update_progress(struct download_context* downloadContext)
{
    int percent;

    percent = (downloadContext->bytes_downloaded * 100) / downloadContext->bytes_total;
    
    // print a fancy progress bar with percentage, upload progress and a moving
    // bar being filled
    printf("\33[2K\r%s/%s [", downloadContext->publisher, downloadContext->package);
    for (int i = 0; i < 20; i++) {
        if (i < percent / 5) {
            printf("#");
        }
        else {
            printf(" ");
        }
    }
    printf("| %3d%%] %6zu / %6zu bytes", percent, 
        downloadContext->bytes_downloaded, 
        downloadContext->bytes_total
    );
    fflush(stdout);
}

static int __download_progress_callback(void *clientp,
    double dltotal, double dlnow,
    double ultotal, double ulnow)
{
    struct download_context* context = (struct download_context*)clientp;
    context->bytes_downloaded = (size_t)dlnow;
    context->bytes_total = (size_t)dltotal;
    if (context->bytes_total > 0) {
        __update_progress(context);
    }
    return 0;
}

static int __get_download_url(struct chef_download_params* params, char* urlBuffer, size_t bufferSize)
{
    // todo specific version support
    int written = snprintf(urlBuffer, bufferSize - 1, 
        "https://chef-api.azurewebsites.net/api/pack/download?publisher=%s&name=%s&platform=%s&arch=%s&channel=%s",
        params->publisher, params->package, params->platform, params->arch, params->channel
    );
    urlBuffer[written] = '\0';
    return written == bufferSize - 1 ? -1 : 0;
}

static int __parse_pack_response(const char* response, struct pack_response* packResponse)
{
    json_error_t error;
    json_t*      root;

    root = json_loads(response, 0, &error);
    if (!root) {
        fprintf(stderr, "__parse_pack_response: failed to parse json: %s\n", error.text);
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
    char     buffer[512];
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

static int __download_file(const char* filePath, struct pack_response* context, struct download_context* downloadContext)
{
    FILE*              file;
    struct curl_slist* headers = NULL;
    int                status  = -1;
    CURL*              curl;
    CURLcode           code;
    long               httpCode;

    // initialize a curl session
    curl = curl_easy_init();
    if (!curl) {
        fprintf(stderr, "__download_file: curl_easy_init() failed\n");
        return -1;
    }
    chef_set_curl_common(curl, (void**)&headers, 0, 1, 0);

    // initialize the output file
    file = fopen(filePath, "wb");
    if (!file) {
        fprintf(stderr, "__download_file: failed to open file [%s]\n", strerror(errno));
        goto cleanup;
    }

    // set required ms headers
    headers = curl_slist_append(headers, "x-ms-blob-type: BlockBlob");
    headers = curl_slist_append(headers, "x-ms-blob-content-type: application/octet-stream");

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

    code = curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0);
    if (code != CURLE_OK) {
        fprintf(stderr, "__download_file: failed to enable download progress [%s]\n", chef_error_buffer());
        goto cleanup;
    }

    code = curl_easy_setopt(curl, CURLOPT_PROGRESSFUNCTION, __download_progress_callback);
    if (code != CURLE_OK) {
        fprintf(stderr, "__download_file: failed to set download progress callback [%s]\n", chef_error_buffer());
        goto cleanup;
    }

    code = curl_easy_setopt(curl, CURLOPT_PROGRESSDATA, downloadContext);
    if (code != CURLE_OK) {
        fprintf(stderr, "__download_file: failed to set download progress callback data [%s]\n", chef_error_buffer());
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
        goto cleanup;
    }
    
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
    if (httpCode < 200 || httpCode >= 300) {
        fprintf(stderr, "__download_file: http error %ld\n", httpCode);
        status = -1;
    }

    status = 0;

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
    struct pack_response    packResponse = { 0 };
    struct download_context downloadContext = { 0 };
    int                     status;

    status = __download_request(params, &packResponse);
    if (status != 0) {
        fprintf(stderr, "chefclient_pack_download: failed to create download request [%s]\n", strerror(errno));
        return status;
    }

    // prepare download context
    downloadContext.publisher = params->publisher;
    downloadContext.package   = params->package;

    // print initial banner
    printf("initiating download of %s/%s", params->publisher, params->package);
    fflush(stdout);

    // start download
    status = __download_file(path, &packResponse, &downloadContext);
    if (status != 0) {
        fprintf(stderr, "chefclient_pack_download: failed to download package [%s]\n", strerror(errno));
        return status;
    }

    // print newline
    printf("\n");
    return status;
}
