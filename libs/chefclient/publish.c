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
#include "../private.h"
#include "base64/base64.h"
#include <stdio.h>
#include <string.h>
#include <vlog.h>

extern const char* chefclient_api_base_url(void);

struct __initiate_response {
    const char* upload_token;
    int         revision;
};

static json_t* __create_publish_request(struct chef_publish_params* params)
{
    json_t* request = json_object();
    if (!request) {
        return NULL;
    }

    json_object_set_new(request, "PublisherName", json_string(params->publisher));
    json_object_set_new(request, "PackageName", json_string(params->package));
    json_object_set_new(request, "Platform", json_string(params->platform));
    json_object_set_new(request, "Architecture", json_string(params->architecture));
    json_object_set_new(request, "Size", json_integer(params->version->size));
    json_object_set_new(request, "Major", json_integer(params->version->major));
    json_object_set_new(request, "Minor", json_integer(params->version->minor));
    json_object_set_new(request, "Patch", json_integer(params->version->patch));
    if (params->version->tag != NULL) {
        json_object_set_new(request, "Tag", json_string(params->architecture));
    }
    return request;
}

static const char* __get_json_string_safe(json_t* object, const char* key)
{
    json_t* value = json_object_get(object, key);
    if (value != NULL && json_string_value(value) != NULL) {
        return platform_strdup(json_string_value(value));
    }
    return NULL;
}

static int __parse_initiate_response(const char* response, struct __initiate_response* context)
{
    json_error_t error;
    json_t*      root;
    
    root = json_loads(response, 0, &error);
    if (root == NULL) {
        return -1;
    }

    context->upload_token = __get_json_string_safe(root, "upload-token");
    context->revision     = (int)json_integer_value(json_object_get(root, "revision"));
    json_decref(root);
    return 0;
}

static int __get_publish_initate_url(char* urlBuffer, size_t bufferSize)
{
    int written = snprintf(urlBuffer, bufferSize - 1, 
        "%s/package/publish/initiate",
        chefclient_api_base_url()
    );
    return written < (bufferSize - 1) ? 0 : -1;
}

static int __get_publish_upload_url(const char* key, char* urlBuffer, size_t bufferSize)
{
    int written = snprintf(urlBuffer, bufferSize - 1, 
        "%s/package/publish/upload?key=%s",
        chefclient_api_base_url(),
        key
    );
    return written < (bufferSize - 1) ? 0 : -1;
}

static int __get_publish_complete_url(const char* key, const char* channel, char* urlBuffer, size_t bufferSize)
{
    int written = snprintf(urlBuffer, bufferSize - 1, 
        "%s/package/publish/complete?key=%s&channel=%s",
        chefclient_api_base_url(),
        key, channel
    );
    return written < (bufferSize - 1) ? 0 : -1;
}

static int __publish_request(json_t* json, struct __initiate_response* context)
{
    struct chef_request* request;
    CURLcode             code;
    char*                body   = NULL;
    int                  status = -1;
    char                 buffer[256];
    long                 httpCode;

    request = chef_request_new(CHEF_CLIENT_API_SECURE, 1);
    if (!request) {
        VLOG_ERROR("chef-client", "__publish_request: failed to create request\n");
        return -1;
    }

    if (__get_publish_initate_url(buffer, sizeof(buffer)) != 0) {
        VLOG_ERROR("chef-client", "__publish_request: buffer too small for publish link\n");
        goto cleanup;
    }

    code = curl_easy_setopt(request->curl, CURLOPT_URL, &buffer[0]);
    if (code != CURLE_OK) {
        VLOG_ERROR("chef-client", "__publish_request: failed to set url [%s]\n", request->error);
        goto cleanup;
    }

    body = json_dumps(json, 0);
    code = curl_easy_setopt(request->curl, CURLOPT_POSTFIELDS, body);
    if (code != CURLE_OK) {
        VLOG_ERROR("chef-client", "__publish_request: failed to set body [%s]\n", request->error);
        goto cleanup;
    }

    code = chef_request_execute(request);
    if (code != CURLE_OK) {
        VLOG_ERROR("chef-client", "__publish_request: chef_request_execute() failed: %s\n", curl_easy_strerror(code));
    }

    curl_easy_getinfo(request->curl, CURLINFO_RESPONSE_CODE, &httpCode);
    if (httpCode != 200) {
        status = -1;
        
        if (httpCode == 401) {
            status = -EACCES;
        } else {
            VLOG_ERROR("chef-client", "__publish_request: http error %ld [%s]\n", httpCode, request->response);
            status = -EIO;
        }
        goto cleanup;
    }

    status = __parse_initiate_response(request->response, context);

cleanup:
    free(body);
    chef_request_delete(request);
    return status;
}

struct file_upload_context {
    FILE*  file;
    size_t length;
    size_t uploaded;
    size_t read;
};

static int __file_upload_context_init(struct file_upload_context* context, const char* path)
{
    context->file = fopen(path, "rb");
    if (context->file == NULL) {
        return -1;
    }

    fseek(context->file, 0, SEEK_END);
    context->length = ftell(context->file);
    
    fseek(context->file, 0, SEEK_SET);
    context->uploaded = 0;
    context->read     = 0;
    return 0;
}

static void __update_progress(struct file_upload_context* context)
{
    int percent = (context->uploaded * 100) / context->length;
    
    // print a fancy progress bar with percentage, upload progress and a moving
    // bar being filled
    printf("\33[2K\r[");
    for (int i = 0; i < 20; i++) {
        if (i < percent / 5) {
            printf("#");
        }
        else {
            printf(" ");
        }
    }
    printf("| %3d%%] %6zu / %6zu bytes", percent, context->uploaded, context->length);
    fflush(stdout);
}

static int __file_seek(void* arg, curl_off_t offset, int origin) {
    struct file_upload_context* context = (struct file_upload_context*)arg;
    return fseek(context->file, (long)offset, origin);
}

static size_t __file_read(char *buffer, size_t size, size_t nitems, void* arg) {
    struct file_upload_context* context = (struct file_upload_context*)arg;
    size_t n = fread(buffer, size, nitems, context->file);
    context->uploaded = context->read;
    context->read += n * size;
    __update_progress(context);
    return n;
}

static void __file_finished(void* arg) {
    struct file_upload_context* context = (struct file_upload_context*)arg;
    context->uploaded = context->read;
    __update_progress(context);
    fclose(context->file);
}

static int __upload_package(const char* path, struct __initiate_response* context)
{
    struct chef_request*       request;
    struct file_upload_context fileContext;
    CURLcode                   code;
    curl_mime*                 multipart = NULL;
    curl_mimepart*             part;
    int                        status = -1;
    char                       buffer[1024];
    long                       httpCode;

    status = __file_upload_context_init(&fileContext, path);
    if (status) {
        VLOG_ERROR("chef-client", "__upload_package: failed to open file %s for upload\n", path);
        return status;
    }

    request = chef_request_new(CHEF_CLIENT_API_SECURE, 1);
    if (!request) {
        VLOG_ERROR("chef-client", "__upload_package: failed to create request\n");
        return -1;
    }

    if (__get_publish_upload_url(context->upload_token, buffer, sizeof(buffer)) != 0) {
        VLOG_ERROR("chef-client", "__upload_package: buffer too small for publish link\n");
        goto cleanup;
    }

    code = curl_easy_setopt(request->curl, CURLOPT_URL, &buffer[0]);
    if (code != CURLE_OK) {
        VLOG_ERROR("chef-client", "__upload_package: failed to set url [%s]\n", request->error);
        goto cleanup;
    }

    multipart = curl_mime_init(request->curl);

    part = curl_mime_addpart(multipart);
    curl_mime_name(part, "sendfile");
    curl_mime_filedata(part, path);
    curl_mime_data_cb(part, fileContext.length, __file_read, __file_seek, __file_finished, &fileContext);

    part = curl_mime_addpart(multipart);
    curl_mime_name(part, "filename");
    curl_mime_data(part, "package.chef", CURL_ZERO_TERMINATED);
    
    part = curl_mime_addpart(multipart);
    curl_mime_name(part, "submit");
    curl_mime_data(part, "send", CURL_ZERO_TERMINATED);

    // initialize custom header list (stating that Expect: 100-continue is not wanted
    //request->headers = curl_slist_append(request->headers, "Expect:");

    code = curl_easy_setopt(request->curl, CURLOPT_MIMEPOST, multipart);
    if (code != CURLE_OK) {
        VLOG_ERROR("chef-client", "__upload_package: failed to set form file [%s]\n", request->error);
        goto cleanup;
    }

    code = chef_request_execute(request);
    if (code != CURLE_OK) {
        VLOG_ERROR("chef-client", "__upload_package: chef_request_execute() failed: %s\n", curl_easy_strerror(code));
    }

    curl_easy_getinfo(request->curl, CURLINFO_RESPONSE_CODE, &httpCode);
    if (httpCode != 200) {
        status = -1;
        
        if (httpCode == 401) {
            status = -EACCES;
        } else {
            VLOG_ERROR("chef-client", "__upload_package: http error %ld [%s]\n", httpCode, request->response);
            status = -EIO;
        }
        goto cleanup;
    }
    status = 0;

cleanup:
    curl_mime_free(multipart);
    chef_request_delete(request);
    return status;
}

static int __publish_complete(const char* channel, struct __initiate_response* context)
{
    struct chef_request* request;
    CURLcode             code;
    int                  status = -1;
    char                 buffer[256];
    long                 httpCode;

    request = chef_request_new(CHEF_CLIENT_API_SECURE, 1);
    if (!request) {
        VLOG_ERROR("chef-client", "__publish_complete: failed to create request\n");
        return -1;
    }

    if (__get_publish_complete_url(context->upload_token, channel, buffer, sizeof(buffer)) != 0) {
        VLOG_ERROR("chef-client", "__publish_complete: buffer too small for publish link\n");
        goto cleanup;
    }

    code = curl_easy_setopt(request->curl, CURLOPT_URL, &buffer[0]);
    if (code != CURLE_OK) {
        VLOG_ERROR("chef-client", "__publish_complete: failed to set url [%s]\n", request->error);
        goto cleanup;
    }

    code = chef_request_execute(request);
    if (code != CURLE_OK) {
        VLOG_ERROR("chef-client", "__publish_complete: chef_request_execute() failed: %s\n", curl_easy_strerror(code));
    }

    curl_easy_getinfo(request->curl, CURLINFO_RESPONSE_CODE, &httpCode);
    if (httpCode != 200) {
        status = -1;
        
        if (httpCode == 401) {
            status = -EACCES;
        }
        else {
            VLOG_ERROR("chef-client", "__publish_complete: http error %ld [%s]\n", httpCode, request->response);
            status = -EIO;
        }
        goto cleanup;
    }

    status = 0;

cleanup:
    chef_request_delete(request);
    return status;
}

int chefclient_pack_publish(struct chef_publish_params* params, const char* path)
{
    struct __initiate_response context = { NULL, 0 };
    json_t*                    request;
    int                        status;

    request = __create_publish_request(params);
    if (!request) {
        VLOG_ERROR("chef-client", "chefclient_pack_publish: failed to create publish request\n");
        status = -1;
        goto cleanup;
    }

    status = __publish_request(request, &context);
    json_decref(request);
    if (status) {
        VLOG_ERROR("chef-client", "chefclient_pack_publish: failed to initiate publish process\n");
        goto cleanup;
    }

    VLOG_TRACE("chef-client", "created revision %i, uploading...\n", context.revision);

    status = __upload_package(path, &context);
    if (status) {
        VLOG_ERROR("chef-client", "chefclient_pack_publish: failed to upload the package for publishing\n");
        goto cleanup;
    }

    VLOG_TRACE("chef-client", "upload complete, publishing revision %i to %s...\n", context.revision, params->channel);

    status = __publish_complete(params->channel, &context);
    if (status) {
        VLOG_ERROR("chef-client", "chefclient_pack_publish: failed to complete publish process\n");
    }

cleanup:
    free((void*)context.upload_token);
    return status;
}
