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
#include <chef/platform.h>
#include <chef/api/package.h>
#include <curl/curl.h>
#include <jansson.h>
#include "private.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vlog.h>

extern const char* chefclient_api_base_url(void);

struct download_context {
    const char* publisher;
    const char* package;
    int         revision;
    size_t      bytes_downloaded;
    size_t      bytes_total;
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

static void __update_progress(struct download_context* downloadContext)
{
    char progressBuffer[32];
    char totalSizeBuffer[32];
    int  percent;

    percent = (downloadContext->bytes_downloaded * 100) / downloadContext->bytes_total;
    
    // print a fancy progress bar with percentage, upload progress and a moving
    // bar being filled
    printf("\33[2K\rdownloading %s/%s [%i] [", downloadContext->publisher, downloadContext->package, downloadContext->revision);
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

static size_t __download_progress_callback(void *clientp,
    curl_off_t dltotal, curl_off_t dlnow,
    curl_off_t ultotal, curl_off_t ulnow)
{
    struct download_context* context = (struct download_context*)clientp;
    context->bytes_downloaded = (size_t)dlnow;
    context->bytes_total = (size_t)dltotal;
    if (context->bytes_total > 0) {
        __update_progress(context);
    }
    return 0;
}

static int __get_download_url(struct download_context* context, char* urlBuffer, size_t bufferSize)
{
    int written = snprintf(urlBuffer, bufferSize - 1, 
        "%s/package/proof?publisher=%s&name=%s&revision=%i",
        chefclient_api_base_url(),
        context->publisher, context->package, context->revision
    );
    return written < (bufferSize - 1) ? 0 : -1;
}

static int __get_publisher_url(struct chef_download_params* params, char* urlBuffer, size_t bufferSize)
{
    int written = snprintf(urlBuffer, bufferSize - 1, 
        "%s/account/publisher?name=%s",
        chefclient_api_base_url(),
        params->publisher, params->package, params->revision
    );
    return written < (bufferSize - 1) ? 0 : -1;
}

static int __download_to_stream(FILE* stream, struct download_context* context)
{
    struct chef_request* request;
    char                 buffer[512];
    int                  status  = -1;
    CURLcode             code;
    long                 httpCode;

    // initialize a curl session
    request = chef_request_new(CHEF_CLIENT_API_SECURE, 0);
    if (!request) {
        VLOG_ERROR("chef-client", "__download_to_stream: failed to create request\n");
        return -1;
    }

    // set the url
    if (__get_download_url(context, buffer, sizeof(buffer)) != 0) {
        VLOG_ERROR("chef-client", "__download_request: buffer too small for package download link\n");
        goto cleanup;
    }

    // reset the writer function/data
    code = curl_easy_setopt(request->curl, CURLOPT_WRITEFUNCTION, fwrite);
    if (code != CURLE_OK) {
        VLOG_ERROR("chef-client", "__download_to_stream: failed to set write function [%s]\n", request->error);
        return -1;
    }

    code = curl_easy_setopt(request->curl, CURLOPT_WRITEDATA, stream);
    if (code != CURLE_OK) {
        VLOG_ERROR("chef-client", "__download_to_stream: failed to set write data [%s]\n", request->error);
        goto cleanup;
    }

    code = curl_easy_setopt(request->curl, CURLOPT_HTTPHEADER, request->headers);
    if (code != CURLE_OK) {
        VLOG_ERROR("chef-client", "__download_to_stream: failed to set http headers [%s]\n", request->error);
        goto cleanup;
    }

    code = curl_easy_setopt(request->curl, CURLOPT_NOPROGRESS, 0);
    if (code != CURLE_OK) {
        VLOG_ERROR("chef-client", "__download_to_stream: failed to enable download progress [%s]\n", request->error);
        goto cleanup;
    }

    code = curl_easy_setopt(request->curl, CURLOPT_XFERINFOFUNCTION, __download_progress_callback);
    if (code != CURLE_OK) {
        VLOG_ERROR("chef-client", "__download_to_stream: failed to set download progress callback [%s]\n", request->error);
        goto cleanup;
    }

    code = curl_easy_setopt(request->curl, CURLOPT_XFERINFODATA, context);
    if (code != CURLE_OK) {
        VLOG_ERROR("chef-client", "__download_to_stream: failed to set download progress callback data [%s]\n", request->error);
        goto cleanup;
    }

    code = curl_easy_setopt(request->curl, CURLOPT_URL, &buffer[0]);
    if (code != CURLE_OK) {
        VLOG_ERROR("chef-client", "__download_to_stream: failed to set url [%s]\n", request->error);
        goto cleanup;
    }

    code = chef_request_execute(request);
    if (code != CURLE_OK) {
        VLOG_ERROR("chef-client", "__download_to_stream: chef_request_execute() failed: %s\n", request->error);
        goto cleanup;
    }
    
    curl_easy_getinfo(request->curl, CURLINFO_RESPONSE_CODE, &httpCode);
    if (httpCode < 200 || httpCode >= 300) {
        VLOG_ERROR("chef-client", "__download_to_stream: http error %ld\n", httpCode);
        status = -1;
    }

    status = 0;

cleanup:
    chef_request_delete(request);
    return status;
}

int chefclient_pack_proof(struct chef_proof_params* params, FILE* stream)
{
    struct download_context downloadContext = { 0 };
    int                     status;

    // prepare download context
    downloadContext.publisher = params->publisher;
    downloadContext.package   = params->package;
    downloadContext.revision  = params->revision;

    // print initial banner
    printf("retrieving proof for %s/%s [%i]", params->publisher, params->package, params->revision);
    fflush(stdout);

    // start download
    status = __download_to_stream(stream, &downloadContext);
    if (status != 0) {
        VLOG_ERROR("chef-client", "chefclient_pack_download: failed to download package [%s]\n", strerror(errno));
        return status;
    }

    // print newline
    printf("\n");
    return status;
}
