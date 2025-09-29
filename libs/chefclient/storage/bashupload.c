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
#include <chef/storage/bashupload.h>
#include <curl/curl.h>
#include <jansson.h>
#include "../private.h"
#include <stdio.h>
#include <string.h>
#include <vlog.h>

#define __BU_URL_BASE "https://bashupload.com/"

struct upload_context {
    // result of upload
    char*  download_url;

    // upload status
    size_t bytes_uploaded;
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

static const char* g_upload0 = "uploading [";

static void __update_progress(struct upload_context* downloadContext)
{
    char progressBuffer[32];
    char totalSizeBuffer[32];
    char buffer[64] = { 0 };
    int  index = 0;
    int  percent;

    percent = (downloadContext->bytes_uploaded * 100) / downloadContext->bytes_total;

    // print a fancy progress bar with percentage, upload progress and a moving
    // bar being filled    
    strcpy(&buffer[0], g_upload0);
    index += strlen(g_upload0);
    for (int i = 0; i < 20; i++) {
        if (i < percent / 5) {
            buffer[index] = '#';
        } else {
            buffer[index] = ' ';
        }
        index++;
    }
    __format_quantity(downloadContext->bytes_uploaded, &buffer[index], sizeof(progressBuffer));
    __format_quantity(downloadContext->bytes_total, &buffer[index], sizeof(totalSizeBuffer));
    VLOG_TRACE("chef-client", "%s | %3d%%] %s / %s",
        &buffer, percent, &progressBuffer[0], &totalSizeBuffer[0]
    );
}

static size_t __upload_progress_callback(void *clientp,
    curl_off_t dltotal, curl_off_t dlnow,
    curl_off_t ultotal, curl_off_t ulnow)
{
    struct upload_context* context = (struct upload_context*)clientp;
    context->bytes_uploaded = (size_t)ulnow;
    context->bytes_total = (size_t)ultotal;
    if (context->bytes_total > 0) {
        __update_progress(context);
    }
    return 0;
}

static int __parse_response(const char* response, struct upload_context* uploadContext)
{
    // Example response:
    // Uploaded 1 file, 7 bytes
    //
    // wget https://bashupload.com/4dcXO/file.txt
    // 
    // =========================
    //
    char* needle = strstr(response, "wget");
    char* end = NULL;
    if (needle == NULL) {
        VLOG_ERROR("chef-client", "__parse_response: could not find 'wget' in %s\n", response);
        return -1;
    }

    // strip the leading and trailing bits
    needle += 5;                                             // skip 'wget '
    for (end = needle; *end != '\0' && *end != '\n'; end++); // find the \n or \0

    uploadContext->download_url = platform_strndup(needle, end - needle);
    if (uploadContext->download_url == NULL) {
        VLOG_ERROR("chef-client", "__parse_response: failed to allocate memory for %s\n", needle);
        return -1;
    }
    return 0;
}

static int __upload_file(const char* filePath, struct upload_context* uploadContext)
{
    struct chef_request* request;
    FILE*                file;
    int                  status  = -1;
    CURLcode             code;
    long                 httpCode;

    request = chef_request_new(1, 0);
    if (!request) {
        VLOG_ERROR("chef-client", "__upload_file: failed to create request\n");
        return -1;
    }

    file = fopen(filePath, "rb");
    if (!file) {
        VLOG_ERROR("chef-client", "__upload_file: failed to open file [%s]\n", strerror(errno));
        goto cleanup;
    }

    code = curl_easy_setopt(request->curl, CURLOPT_UPLOAD, 1L);
    if (code != CURLE_OK) {
        VLOG_ERROR("chef-client", "__upload_file: failed to set upload type [%s]\n", request->error);
        return -1;
    }

    code = curl_easy_setopt(request->curl, CURLOPT_READFUNCTION, fread);
    if (code != CURLE_OK) {
        VLOG_ERROR("chef-client", "__upload_file: failed to set read function [%s]\n", request->error);
        return -1;
    }

    code = curl_easy_setopt(request->curl, CURLOPT_READDATA, file);
    if (code != CURLE_OK) {
        VLOG_ERROR("chef-client", "__upload_file: failed to set read data [%s]\n", request->error);
        goto cleanup;
    }

    code = curl_easy_setopt(request->curl, CURLOPT_HTTPHEADER, request->headers);
    if (code != CURLE_OK) {
        VLOG_ERROR("chef-client", "__upload_file: failed to set http headers [%s]\n", request->error);
        goto cleanup;
    }

    code = curl_easy_setopt(request->curl, CURLOPT_NOPROGRESS, 0);
    if (code != CURLE_OK) {
        VLOG_ERROR("chef-client", "__upload_file: failed to enable upload progress [%s]\n", request->error);
        goto cleanup;
    }

    code = curl_easy_setopt(request->curl, CURLOPT_XFERINFOFUNCTION, __upload_progress_callback);
    if (code != CURLE_OK) {
        VLOG_ERROR("chef-client", "__upload_file: failed to set download progress callback [%s]\n", request->error);
        goto cleanup;
    }

    code = curl_easy_setopt(request->curl, CURLOPT_XFERINFODATA, uploadContext);
    if (code != CURLE_OK) {
        VLOG_ERROR("chef-client", "__upload_file: failed to set download progress callback data [%s]\n", request->error);
        goto cleanup;
    }

    code = curl_easy_setopt(request->curl, CURLOPT_URL, __BU_URL_BASE);
    if (code != CURLE_OK) {
        VLOG_ERROR("chef-client", "__upload_file: failed to set url [%s]\n", request->error);
        goto cleanup;
    }

    code = chef_request_execute(request);
    if (code != CURLE_OK) {
        VLOG_ERROR("chef-client", "__upload_file: chef_request_execute() failed: %s\n", request->error);
        goto cleanup;
    }
    
    curl_easy_getinfo(request->curl, CURLINFO_RESPONSE_CODE, &httpCode);
    if (httpCode < 200 || httpCode >= 300) {
        VLOG_ERROR("chef-client", "__upload_file: http error %ld\n", httpCode);
        status = -1;
    }

    status = __parse_response(request->response, uploadContext);

cleanup:
    if (file) {
        fclose(file);
    }
    chef_request_delete(request);
    return status;
}

int chef_client_bu_upload(const char* path, char** downloadUrl)
{
    struct upload_context uploadContext = { 0 };
    int                   status;

    vlog_set_output_options(stdout, VLOG_OUTPUT_OPTION_PROGRESS);
    status = __upload_file(path, &uploadContext);
    vlog_clear_output_options(stdout, VLOG_OUTPUT_OPTION_PROGRESS);
    if (status != 0) {
        VLOG_ERROR("chef-client", "chef_client_bu_upload: failed to upload file [%s]\n", strerror(errno));
        return status;
    }

    // okay success, let us set the out
    *downloadUrl = uploadContext.download_url;
    return 0;
}
