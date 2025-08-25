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

struct pack_response {
    int         revision;
    const char* token;
    const char* url;
};

struct download_context {
    const char* publisher;
    const char* package;
    char        version[32];
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
    printf("\33[2K\rdownloading %s/%s (%s) [", downloadContext->publisher, downloadContext->package, downloadContext->version);
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

static int __get_download_url(struct chef_download_params* params, char* urlBuffer, size_t bufferSize)
{
    // todo specific version support
    int written = snprintf(urlBuffer, bufferSize - 1, 
        "%s/pack/download?publisher=%s&name=%s&platform=%s&arch=%s&channel=%s",
        chefclient_api_base_url(),
        params->publisher, params->package, params->platform, params->arch, params->channel
    );
    return written < (bufferSize - 1) ? 0 : -1;
}

static int __parse_pack_response(const char* response, struct pack_response* packResponse)
{
    json_error_t error;
    json_t*      root;

    root = json_loads(response, 0, &error);
    if (!root) {
        VLOG_ERROR("chef-client", "__parse_pack_response: failed to parse json: %s\n", error.text);
        return -1;
    }

    packResponse->revision = json_integer_value(json_object_get(root, "pack-revision"));
    packResponse->token = platform_strdup(json_string_value(json_object_get(root, "sas-token")));
    packResponse->url = platform_strdup(json_string_value(json_object_get(root, "blob-url")));
    json_decref(root);

    return 0;
}

int __download_request(struct chef_download_params* params, struct pack_response* packResponse)
{
    struct chef_request* request;
    CURLcode             code;
    char                 buffer[512];
    int                  status = -1;
    long                 httpCode;

    // initialize a curl session
    request = chef_request_new(1, 0);
    if (!request) {
        VLOG_ERROR("chef-client", "__download_request: failed to create request\n");
        return -1;
    }

    // set the url
    if (__get_download_url(params, buffer, sizeof(buffer)) != 0) {
        VLOG_ERROR("chef-client", "__download_request: buffer too small for package download link\n");
        goto cleanup;
    }

    code = curl_easy_setopt(request->curl, CURLOPT_URL, &buffer[0]);
    if (code != CURLE_OK) {
        VLOG_ERROR("chef-client", "__download_request: failed to set url [%s]\n", request->error);
        goto cleanup;
    }

    code = curl_easy_perform(request->curl);
    if (code != CURLE_OK) {
        VLOG_ERROR("chef-client", "__download_request: curl_easy_perform() failed: %s\n", curl_easy_strerror(code));
    }

    curl_easy_getinfo(request->curl, CURLINFO_RESPONSE_CODE, &httpCode);
    if (httpCode != 200) {
        status = -1;
        
        if (httpCode == 404) {
            VLOG_ERROR("chef-client", "__download_request: package not found\n");
            errno = ENOENT;
        }
        else {
            VLOG_ERROR("chef-client", "__download_request: http error %ld [%s]\n", httpCode, request->response);
            errno = EIO;
        }
        goto cleanup;
    }

    status = __parse_pack_response(request->response, packResponse);

cleanup:
    chef_request_delete(request);
    return status;
}

static int __download_file(const char* filePath, struct pack_response* context, struct download_context* downloadContext)
{
    struct chef_request* request;
    FILE*                file;
    int                  status  = -1;
    CURLcode             code;
    long                 httpCode;

    // initialize a curl session
    request = chef_request_new(1, 0);
    if (!request) {
        VLOG_ERROR("chef-client", "__download_file: failed to create request\n");
        return -1;
    }

    // initialize the output file
    file = fopen(filePath, "wb");
    if (!file) {
        VLOG_ERROR("chef-client", "__download_file: failed to open file [%s]\n", strerror(errno));
        goto cleanup;
    }

    // set required ms headers
    request->headers = curl_slist_append(request->headers, "x-ms-blob-type: BlockBlob");
    request->headers = curl_slist_append(request->headers, "x-ms-blob-content-type: application/octet-stream");

    // reset the writer function/data
    code = curl_easy_setopt(request->curl, CURLOPT_WRITEFUNCTION, fwrite);
    if (code != CURLE_OK) {
        VLOG_ERROR("chef-client", "__download_file: failed to set write function [%s]\n", request->error);
        return -1;
    }

    code = curl_easy_setopt(request->curl, CURLOPT_WRITEDATA, file);
    if (code != CURLE_OK) {
        VLOG_ERROR("chef-client", "__download_file: failed to set write data [%s]\n", request->error);
        goto cleanup;
    }

    code = curl_easy_setopt(request->curl, CURLOPT_HTTPHEADER, request->headers);
    if (code != CURLE_OK) {
        VLOG_ERROR("chef-client", "__download_file: failed to set http headers [%s]\n", request->error);
        goto cleanup;
    }

    code = curl_easy_setopt(request->curl, CURLOPT_NOPROGRESS, 0);
    if (code != CURLE_OK) {
        VLOG_ERROR("chef-client", "__download_file: failed to enable download progress [%s]\n", request->error);
        goto cleanup;
    }

    code = curl_easy_setopt(request->curl, CURLOPT_XFERINFOFUNCTION, __download_progress_callback);
    if (code != CURLE_OK) {
        VLOG_ERROR("chef-client", "__download_file: failed to set download progress callback [%s]\n", request->error);
        goto cleanup;
    }

    code = curl_easy_setopt(request->curl, CURLOPT_XFERINFODATA, downloadContext);
    if (code != CURLE_OK) {
        VLOG_ERROR("chef-client", "__download_file: failed to set download progress callback data [%s]\n", request->error);
        goto cleanup;
    }

    code = curl_easy_setopt(request->curl, CURLOPT_URL, context->url);
    if (code != CURLE_OK) {
        VLOG_ERROR("chef-client", "__download_file: failed to set url [%s]\n", request->error);
        goto cleanup;
    }

    code = curl_easy_perform(request->curl);
    if (code != CURLE_OK) {
        VLOG_ERROR("chef-client", "__download_file: curl_easy_perform() failed: %s\n", request->error);
        goto cleanup;
    }
    
    curl_easy_getinfo(request->curl, CURLINFO_RESPONSE_CODE, &httpCode);
    if (httpCode < 200 || httpCode >= 300) {
        VLOG_ERROR("chef-client", "__download_file: http error %ld\n", httpCode);
        status = -1;
    }

    status = 0;

cleanup:
    if (file) {
        fclose(file);
    }
    chef_request_delete(request);
    return status;
}

static void __format_version(char* buffer, struct chef_version* version, int revision)
{
    if (version != NULL) {
        sprintf(&buffer[0], "%i.%i.%i", version->major, version->minor, version->patch);
    } else {
        sprintf(&buffer[0], "%i", revision);
    }
}

int chefclient_pack_download(struct chef_download_params* params, const char* path)
{
    struct pack_response    packResponse = { 0 };
    struct download_context downloadContext = { 0 };
    int                     status;

    status = __download_request(params, &packResponse);
    if (status != 0) {
        VLOG_ERROR("chef-client", "chefclient_pack_download: failed to create download request [%s]\n", strerror(errno));
        return status;
    }

    // update the revision
    params->revision = packResponse.revision;

    // prepare download context
    downloadContext.publisher = params->publisher;
    downloadContext.package   = params->package;
    __format_version(&downloadContext.version[0], params->version, packResponse.revision);

    // print initial banner
    printf("initiating download of %s/%s", params->publisher, params->package);
    fflush(stdout);

    // start download
    status = __download_file(path, &packResponse, &downloadContext);
    if (status != 0) {
        VLOG_ERROR("chef-client", "chefclient_pack_download: failed to download package [%s]\n", strerror(errno));
        return status;
    }

    // print newline
    printf("\n");
    return status;
}
