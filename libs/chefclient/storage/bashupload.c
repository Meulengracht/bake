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
#include <chef/platform.h>
#include <chef/storage/bashupload.h>
#include <curl/curl.h>
#include <jansson.h>
#include "../private.h"
#include <stdio.h>
#include <string.h>

#define __BU_URL_BASE "https://bashupload.com/"

struct pack_response {
    const char* url;
};

struct upload_context {
    size_t bytes_downloaded;
    size_t bytes_total;
};

static void __format_quantity(long long size, char* buffer, size_t bufferSize)
{
	char*  suffix[]       = { "B", "KB", "MB", "GB", "TB" };
    int    i              = 0;
	double remainingBytes = (double)size;

	if (size >= 1024) {
        long long count = size;
		for (; (count / 1024) > 0 && i < 4 /* len(suffix)-1 */; i++, count /= 1024) {
			remainingBytes = count / 1024.0;
        }
	}
	snprintf(&buffer[0], bufferSize, "%.02lf%s", remainingBytes, suffix[i]);
}

static void __update_progress(struct upload_context* downloadContext)
{
    char progressBuffer[32];
    char totalSizeBuffer[32];
    int  percent;

    percent = (downloadContext->bytes_downloaded * 100) / downloadContext->bytes_total;
    
    // print a fancy progress bar with percentage, upload progress and a moving
    // bar being filled
    printf("\33[2K\rdownloading [");
    for (int i = 0; i < 20; i++) {
        if (i < percent / 5) {
            printf("#");
        }
        else {
            printf(" ");
        }
    }
    __format_quantity(downloadContext->bytes_downloaded, &progressBuffer[0], sizeof(progressBuffer));
    __format_quantity(downloadContext->bytes_total, &totalSizeBuffer[0], sizeof(totalSizeBuffer));
    printf("| %3d%%] %s / %s", percent, 
        &progressBuffer[0], &totalSizeBuffer[0]
    );
    fflush(stdout);
}

static int __upload_progress_callback(void *clientp,
    double dltotal, double dlnow,
    double ultotal, double ulnow)
{
    struct upload_context* context = (struct upload_context*)clientp;
    context->bytes_downloaded = (size_t)dlnow;
    context->bytes_total = (size_t)dltotal;
    if (context->bytes_total > 0) {
        __update_progress(context);
    }
    return 0;
}

static int __upload_file(const char* filePath, struct pack_response* context, struct upload_context* uploadContext)
{
    struct chef_request* request;
    FILE*                file;
    int                  status  = -1;
    CURLcode             code;
    long                 httpCode;

    // initialize a curl session
    request = chef_request_new(1, 0);
    if (!request) {
        fprintf(stderr, "__upload_file: failed to create request\n");
        return -1;
    }

    // initialize the output file
    file = fopen(filePath, "rb");
    if (!file) {
        fprintf(stderr, "__upload_file: failed to open file [%s]\n", strerror(errno));
        goto cleanup;
    }

    code = curl_easy_setopt(request->curl, CURLOPT_UPLOAD, 1L);
    if (code != CURLE_OK) {
        fprintf(stderr, "__upload_file: failed to set upload type [%s]\n", request->error);
        return -1;
    }

    code = curl_easy_setopt(request->curl, CURLOPT_READFUNCTION, fread);
    if (code != CURLE_OK) {
        fprintf(stderr, "__upload_file: failed to set read function [%s]\n", request->error);
        return -1;
    }

    code = curl_easy_setopt(request->curl, CURLOPT_READDATA, file);
    if (code != CURLE_OK) {
        fprintf(stderr, "__upload_file: failed to set read data [%s]\n", request->error);
        goto cleanup;
    }

    code = curl_easy_setopt(request->curl, CURLOPT_HTTPHEADER, request->headers);
    if (code != CURLE_OK) {
        fprintf(stderr, "__upload_file: failed to set http headers [%s]\n", request->error);
        goto cleanup;
    }

    code = curl_easy_setopt(request->curl, CURLOPT_NOPROGRESS, 0);
    if (code != CURLE_OK) {
        fprintf(stderr, "__upload_file: failed to enable upload progress [%s]\n", request->error);
        goto cleanup;
    }

    code = curl_easy_setopt(request->curl, CURLOPT_PROGRESSFUNCTION, __upload_progress_callback);
    if (code != CURLE_OK) {
        fprintf(stderr, "__upload_file: failed to set download progress callback [%s]\n", request->error);
        goto cleanup;
    }

    code = curl_easy_setopt(request->curl, CURLOPT_PROGRESSDATA, uploadContext);
    if (code != CURLE_OK) {
        fprintf(stderr, "__upload_file: failed to set download progress callback data [%s]\n", request->error);
        goto cleanup;
    }

    code = curl_easy_setopt(request->curl, CURLOPT_URL, context->url);
    if (code != CURLE_OK) {
        fprintf(stderr, "__upload_file: failed to set url [%s]\n", request->error);
        goto cleanup;
    }

    code = curl_easy_perform(request->curl);
    if (code != CURLE_OK) {
        fprintf(stderr, "__upload_file: curl_easy_perform() failed: %s\n", request->error);
        goto cleanup;
    }
    
    curl_easy_getinfo(request->curl, CURLINFO_RESPONSE_CODE, &httpCode);
    if (httpCode < 200 || httpCode >= 300) {
        fprintf(stderr, "__upload_file: http error %ld\n", httpCode);
        status = -1;
    }

    status = __parse_pack_response(request->response, context);

cleanup:
    if (file) {
        fclose(file);
    }
    chef_request_delete(request);
    return status;
}

int chef_client_bu_upload(const char* path, char** downloadUrl)
{
    struct pack_response  packResponse = { 0 };
    struct upload_context uploadContext = { 0 };
    int                   status;

    // print initial banner
    printf("initiating upload of %s", path);
    fflush(stdout);

    // start download
    status = __upload_file(path, &packResponse, &uploadContext);
    if (status != 0) {
        fprintf(stderr, "chefclient_pack_download: failed to download package [%s]\n", strerror(errno));
        return status;
    }
    return 0;
}
