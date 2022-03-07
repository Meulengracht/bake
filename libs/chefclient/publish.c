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
#include "../private.h"
#include <stdio.h>
#include <string.h>

struct pack_response {
    const char* token;
    const char* url;
};

struct file_upload_context {
    FILE*  file;
    size_t length;
    size_t bytes_read;
};

static json_t* __create_pack_info(struct chef_publish_params* params)
{
    json_t* packInfo = json_object();
    if (!packInfo) {
        return NULL;
    }

    json_object_set_new(packInfo, "name", json_string(params->package->package));
    json_object_set_new(packInfo, "description", json_string(params->package->description));
    json_object_set_new(packInfo, "homepage", json_string(params->package->homepage));
    json_object_set_new(packInfo, "license", json_string(params->package->license));
    json_object_set_new(packInfo, "maintainer", json_string(params->package->maintainer));
    json_object_set_new(packInfo, "maintainer_email", json_string(params->package->maintainer_email));
    return packInfo;
}

static json_t* __create_pack_version(struct chef_publish_params* params)
{
    json_t* packVersion = json_object();
    if (!packVersion) {
        return NULL;
    }

    json_object_set_new(packVersion, "major", json_integer(params->version->major));
    json_object_set_new(packVersion, "minor", json_integer(params->version->minor));

    // revision is only set by server, and ignored by us
    json_object_set_new(packVersion, "revision", json_integer(0));
    
    if (params->version->tag) {
        json_object_set_new(packVersion, "additional", json_string(params->version->tag));
    }
    return packVersion;
}

static json_t* __create_publish_request(struct chef_publish_params* params)
{
    json_t* request = json_object();
    if (!request) {
        return NULL;
    }

    json_object_set_new(request, "info", __create_pack_info(params));
    json_object_set_new(request, "channel", json_string(params->channel));
    json_object_set_new(request, "version", __create_pack_version(params));
    return request;
}

static json_t* __create_commit_request(struct chef_publish_params* params)
{
    json_t* request = json_object();
    if (!request) {
        return NULL;
    }

    json_object_set_new(request, "info", __create_pack_info(params));
    json_object_set_new(request, "channel", json_string(params->channel));
    json_object_set_new(request, "version", __create_pack_version(params));
    return request;
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

static int __get_publish_url(char* urlBuffer, size_t bufferSize)
{
    int written = snprintf(urlBuffer, bufferSize - 1, 
        "https://chef-api.azurewebsites.net/api/PackPublishPrepare"
    );
    urlBuffer[written] = '\0';
    return written == bufferSize - 1 ? -1 : 0;
}

static int __get_commit_url(char* urlBuffer, size_t bufferSize)
{
    int written = snprintf(urlBuffer, bufferSize - 1,
    "https://chef-api.azurewebsites.net/api/PackPublishCommit"
    );
    urlBuffer[written] = '\0';
    return written == bufferSize - 1 ? -1 : 0;
}

static int __publish_request(json_t* json, struct pack_response* context)
{
    CURL*              curl;
    CURLcode           code;
    struct curl_slist* headers   = NULL;
    size_t             dataIndex = 0;
    char*              body      = NULL;
    int                status    = -1;
    char               buffer[256];

    // initialize a curl session
    curl = curl_easy_init();
    if (!curl) {
        fprintf(stderr, "__publish_request: curl_easy_init() failed\n");
        return -1;
    }
    chef_set_curl_common(curl, (void**)&headers, 1, 1, 1);

    // set the url
    if (__get_publish_url(buffer, sizeof(buffer)) != 0) {
        fprintf(stderr, "__publish_request: buffer too small for device code auth link\n");
        goto cleanup;
    }

    code = curl_easy_setopt(curl, CURLOPT_URL, &buffer[0]);
    if (code != CURLE_OK) {
        fprintf(stderr, "__publish_request: failed to set url [%s]\n", chef_error_buffer());
        goto cleanup;
    }

    code = curl_easy_setopt(curl, CURLOPT_WRITEDATA, &dataIndex);
    if(code != CURLE_OK) {
        fprintf(stderr, "__publish_request: failed to set write data [%s]\n", chef_error_buffer());
        goto cleanup;
    }

    body = json_dumps(json, 0);
    code = curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
    if (code != CURLE_OK) {
        fprintf(stderr, "__publish_request: failed to set body [%s]\n", chef_error_buffer());
        goto cleanup;
    }

    code = curl_easy_perform(curl);
    if (code != CURLE_OK) {
        fprintf(stderr, "__publish_request: curl_easy_perform() failed: %s\n", curl_easy_strerror(code));
    }

    status = __parse_pack_response(chef_response_buffer(), context);

cleanup:
    free(body);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return status;
}

static int __init_file_context(struct file_upload_context* context, const char* filename)
{
    context->file = fopen(filename, "rb");
    if (!context->file) {
        return -1;
    }

    fseek(context->file, 0, SEEK_END);
    context->length = ftell(context->file);
    fseek(context->file, 0, SEEK_SET);
    return 0;
}

static void __cleanup_file_context(struct file_upload_context* context)
{
    if (context->file) {
        fclose(context->file);
    }
}

static size_t __read_file(char *ptr, size_t size, size_t nmemb, void *userp)
{
    struct file_upload_context *i = userp;
    size_t retcode = fread(ptr, size, nmemb, i->file);
    i->bytes_read += retcode;
    return retcode;
}

static int __upload_file(const char* filePath, struct pack_response* context)
{
    struct file_upload_context fileContext = { 0 };
    struct curl_slist*         headers     = NULL;
    int                        status      = -1;
    CURL*                      curl;
    CURLcode                   code;

    // initialize a curl session
    curl = curl_easy_init();
    if (!curl) {
        fprintf(stderr, "__upload_file: curl_easy_init() failed\n");
        return -1;
    }
    chef_set_curl_common(curl, NULL, 0, 1, 1);

    status = __init_file_context(&fileContext, filePath);
    if (status != 0) {
        fprintf(stderr, "__upload_file: failed to initialize file context\n");
        goto cleanup;
    }

    // set required ms headers
    headers = curl_slist_append(headers, "x-ms-blob-type:BlockBlob");
    headers = curl_slist_append(headers, "x-ms-blob-content-type:application/octet-stream");

    code = curl_easy_setopt(curl, CURLOPT_READFUNCTION, __read_file);
    if (code != CURLE_OK) {
        fprintf(stderr, "__upload_file: failed to set upload data reader [%s]\n", chef_error_buffer());
        goto cleanup;
    }

    code = curl_easy_setopt(curl, CURLOPT_READDATA, &fileContext);
    if (code != CURLE_OK) {
        fprintf(stderr, "__upload_file: failed to set upload data [%s]\n", chef_error_buffer());
        goto cleanup;
    }

    code = curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE, fileContext.length);
    if (code != CURLE_OK) {
        fprintf(stderr, "__upload_file: failed to set upload size [%s]\n", chef_error_buffer());
        goto cleanup;
    }

    code = curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
    if (code != CURLE_OK) {
        fprintf(stderr, "__upload_file: failed to set request to upload [%s]\n", chef_error_buffer());
        goto cleanup;
    }

    code = curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    if (code != CURLE_OK) {
        fprintf(stderr, "__upload_file: failed to set http headers [%s]\n", chef_error_buffer());
        goto cleanup;
    }

    code = curl_easy_setopt(curl, CURLOPT_URL, context->url);
    if (code != CURLE_OK) {
        fprintf(stderr, "__upload_file: failed to set url [%s]\n", chef_error_buffer());
        goto cleanup;
    }

    code = curl_easy_perform(curl);
    if (code != CURLE_OK) {
        fprintf(stderr, "__upload_file: curl_easy_perform() failed: %s\n", chef_error_buffer());
    }

cleanup:
    __cleanup_file_context(&fileContext);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return status;
}

static int __commit_request(json_t* json)
{
    CURL*              curl;
    CURLcode           code;
    struct curl_slist* headers   = NULL;
    size_t             dataIndex = 0;
    char*              body      = NULL;
    int                status    = -1;
    char               buffer[256];

    // initialize a curl session
    curl = curl_easy_init();
    if (!curl) {
        fprintf(stderr, "__commit_request: curl_easy_init() failed\n");
        return -1;
    }
    chef_set_curl_common(curl, (void**)&headers, 1, 1, 1);

    // set the url
    if (__get_commit_url(buffer, sizeof(buffer)) != 0) {
        fprintf(stderr, "__commit_request: buffer too small for device code auth link\n");
        goto cleanup;
    }

    code = curl_easy_setopt(curl, CURLOPT_URL, &buffer[0]);
    if (code != CURLE_OK) {
        fprintf(stderr, "__commit_request: failed to set url [%s]\n", chef_error_buffer());
        goto cleanup;
    }

    code = curl_easy_setopt(curl, CURLOPT_WRITEDATA, &dataIndex);
    if(code != CURLE_OK) {
        fprintf(stderr, "__commit_request: failed to set write data [%s]\n", chef_error_buffer());
        goto cleanup;
    }

    body = json_dumps(json, 0);
    code = curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
    if (code != CURLE_OK) {
        fprintf(stderr, "__commit_request: failed to set body [%s]\n", chef_error_buffer());
        goto cleanup;
    }

    code = curl_easy_perform(curl);
    if (code != CURLE_OK) {
        fprintf(stderr, "__commit_request: curl_easy_perform() failed: %s\n", curl_easy_strerror(code));
    }

    status = 0;

cleanup:
    free(body);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return status;
}

int chefclient_pack_publish(struct chef_publish_params* params, const char* path)
{
    struct pack_response context;
    json_t*              request;
    int                  status;

    // create the publish request
    request = __create_publish_request(params);
    if (!request) {
        fprintf(stderr, "chefclient_pack_publish: failed to create publish request\n");
        return -1;
    }

    // build and execute the curl request
    status = __publish_request(request, &context);
    if (status != 0) {
        fprintf(stderr, "chefclient_pack_publish: failed to publish pack\n");
        return -1;
    }
    json_decref(request);

    // now upload the pack
    status = __upload_file(path, &context);
    if (status != 0) {
        fprintf(stderr, "chefclient_pack_publish: failed to upload pack\n");
        return -1;
    }

    // create the commit request
    request = __create_commit_request(params);
    if (!request) {
        fprintf(stderr, "chefclient_pack_publish: failed to create commit request\n");
        return -1;
    }

    // and finally commit the new pack version
    status = __commit_request(request);
    if (status != 0) {
        fprintf(stderr, "chefclient_pack_publish: failed to commit pack\n");
        return -1;
    }
    json_decref(request);
    return 0;
}
