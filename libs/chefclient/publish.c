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

// Max supported by REST api on azure is 100mb
#define CHEF_UPLOAD_MAX_SIZE (100 * 1024 * 1024)

struct pack_response {
    const char* token;
    const char* url;
};

struct file_upload_context {
    CURL*  handle;
    FILE*  file;
    size_t length;
    size_t bytes_read;
};

static int __create_file_contexts(const char* path, struct file_upload_context** contextsOut, int* contextCountOut)
{
    // Allow each segment to be uploaded in parallel, max size of 100mb
    int                         contextCount = 0;
    struct file_upload_context* contexts = NULL;
    FILE*                       file = fopen(path, "rb");
    size_t                      fileSize;

    if (!file) {
        return -1;
    }

    fseek(file, 0, SEEK_END);
    fileSize = ftell(file);
    fseek(file, 0, SEEK_SET);
    fclose(file);

    // Calculate the number of segments
    contextCount = (int)((fileSize + (CHEF_UPLOAD_MAX_SIZE - 1)) / CHEF_UPLOAD_MAX_SIZE);

    // Allocate contexts
    contexts = (struct file_upload_context*)malloc(sizeof(struct file_upload_context) * contextCount);
    if (!contexts) {
        return -1;
    }

    // Initialize contexts
    for (long i = 0; i < (long)contextCount; i++) {
        contexts[i].file = fopen(path, "rb");
        if (!contexts[i].file) {
            return -1;
        }
        fseek(contexts[i].file, i * CHEF_UPLOAD_MAX_SIZE, SEEK_SET);

        contexts[i].length = (i == (contextCount - 1)) ? (fileSize - (i * CHEF_UPLOAD_MAX_SIZE)) : CHEF_UPLOAD_MAX_SIZE;
        contexts[i].bytes_read = 0;
        contexts[i].handle = NULL;
    }

    *contextsOut = contexts;
    *contextCountOut = contextCount;
    return 0;
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

    json_object_set_new(request, "name", json_string(params->package->package));
    json_object_set_new(request, "platform", json_string(params->platform));
    json_object_set_new(request, "architecture", json_string(params->arch));
    json_object_set_new(request, "channel", json_string(params->channel));
    return request;
}

static json_t* __create_commit_request(struct chef_publish_params* params)
{
    json_t* request = json_object();
    if (!request) {
        return NULL;
    }

    json_object_set_new(request, "name", json_string(params->package->package));
    json_object_set_new(request, "description", json_string(params->package->description));
    json_object_set_new(request, "homepage", json_string(params->package->homepage));
    json_object_set_new(request, "license", json_string(params->package->license));
    json_object_set_new(request, "maintainer", json_string(params->package->maintainer));
    json_object_set_new(request, "maintainer_email", json_string(params->package->maintainer_email));
    json_object_set_new(request, "platform", json_string(params->platform));
    json_object_set_new(request, "architecture", json_string(params->arch));
    json_object_set_new(request, "channel", json_string(params->channel));
    json_object_set_new(request, "version", __create_pack_version(params));
    return request;
}

static int __parse_pack_response(const char* response, struct pack_response* packResponse)
{
    json_error_t error;
    json_t*      root;
    
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
        "https://chef-api.azurewebsites.net/api/pack/publish"
    );
    urlBuffer[written] = '\0';
    return written == bufferSize - 1 ? -1 : 0;
}

static int __get_commit_url(char* urlBuffer, size_t bufferSize)
{
    int written = snprintf(urlBuffer, bufferSize - 1,
        "https://chef-api.azurewebsites.net/api/pack/commit"
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
    long               httpCode;

    // initialize a curl session
    curl = curl_easy_init();
    if (!curl) {
        fprintf(stderr, "__publish_request: curl_easy_init() failed\n");
        return -1;
    }
    chef_set_curl_common(curl, (void**)&headers, 1, 1, 1);

    // set the url
    if (__get_publish_url(buffer, sizeof(buffer)) != 0) {
        fprintf(stderr, "__publish_request: buffer too small for publish link\n");
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

    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
    if (httpCode != 200) {
        status = -1;
        
        if (httpCode == 302) {
            status = -EACCES;
        }
        else {
            fprintf(stderr, "__publish_request: http error %ld [%s]\n", httpCode, chef_response_buffer());
            status = -EIO;
        }
        goto cleanup;
    }

    status = __parse_pack_response(chef_response_buffer(), context);

cleanup:
    free(body);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return status;
}

static int __write_blocklist(json_t* json, struct pack_response* context)
{
    CURL*              curl;
    CURLcode           code;
    struct curl_slist* headers   = NULL;
    size_t             dataIndex = 0;
    char*              body      = NULL;
    int                status    = -1;
    char               buffer[256];
    long               httpCode;

    // initialize a curl session
    curl = curl_easy_init();
    if (!curl) {
        fprintf(stderr, "__publish_request: curl_easy_init() failed\n");
        return -1;
    }
    chef_set_curl_common(curl, (void**)&headers, 1, 1, 1);

    // set the url
    if (__get_publish_url(buffer, sizeof(buffer)) != 0) {
        fprintf(stderr, "__publish_request: buffer too small for publish link\n");
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

    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
    if (httpCode != 200) {
        status = -1;
        
        if (httpCode == 302) {
            status = -EACCES;
        }
        else {
            fprintf(stderr, "__publish_request: http error %ld [%s]\n", httpCode, chef_response_buffer());
            status = -EIO;
        }
        goto cleanup;
    }

    status = __parse_pack_response(chef_response_buffer(), context);

cleanup:
    free(body);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return status;
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

static int __generate_bad_but_valid_guid(void)
{
    srand (clock());
    char GUID[40];
    int t = 0;
    char *szTemp = "xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx";
    char *szHex = "0123456789ABCDEF-";
    int nLen = strlen (szTemp);

    for (t=0; t<nLen+1; t++)
    {
        int r = rand () % 16;
        char c = ' ';   

        switch (szTemp[t])
        {
            case 'x' : { c = szHex [r]; } break;
            case 'y' : { c = szHex [r & 0x03 | 0x08]; } break;
            case '-' : { c = '-'; } break;
            case '4' : { c = '4'; } break;
        }

        GUID[t] = ( t < nLen ) ? c : 0x00;
    }

    printf ("%s\r\n", GUID);
}

// https://inside.covve.com/uploading-block-blobs-larger-than-256-mb-in-azure/
// Split file into blocks of 100mb
// Generate a GUID for each block
// Convert to base64

// Upload each block to {sas-url}&comp=block&blockid={base64BlockId}
// Upload block list to {sasUri}&comp=blocklist

// Delete blocks

static int __upload_block(struct curl_slist* headers, struct file_upload_context* uploadContext, struct pack_response* context)
{
    int                status      = -1;
    CURLcode           code;
    long               httpCode;

    // initialize a curl session
    uploadContext->handle = curl_easy_init();
    if (!uploadContext->handle) {
        fprintf(stderr, "__upload_block: curl_easy_init() failed\n");
        return -1;
    }
    chef_set_curl_common(uploadContext->handle, NULL, 0, 1, 0);

    code = curl_easy_setopt(uploadContext->handle, CURLOPT_READFUNCTION, __read_file);
    if (code != CURLE_OK) {
        fprintf(stderr, "__upload_block: failed to set upload data reader [%s]\n", chef_error_buffer());
        goto cleanup;
    }

    code = curl_easy_setopt(uploadContext->handle, CURLOPT_READDATA, uploadContext);
    if (code != CURLE_OK) {
        fprintf(stderr, "__upload_block: failed to set upload data [%s]\n", chef_error_buffer());
        goto cleanup;
    }

    code = curl_easy_setopt(uploadContext->handle, CURLOPT_INFILESIZE_LARGE, uploadContext->length);
    if (code != CURLE_OK) {
        fprintf(stderr, "__upload_block: failed to set upload size [%s]\n", chef_error_buffer());
        goto cleanup;
    }

    code = curl_easy_setopt(uploadContext->handle, CURLOPT_UPLOAD, 1L);
    if (code != CURLE_OK) {
        fprintf(stderr, "__upload_block: failed to set request to upload [%s]\n", chef_error_buffer());
        goto cleanup;
    }

    code = curl_easy_setopt(uploadContext->handle, CURLOPT_NOPROGRESS, 0);
    if (code != CURLE_OK) {
        fprintf(stderr, "__upload_block: failed to enable upload progress [%s]\n", chef_error_buffer());
        goto cleanup;
    }

    code = curl_easy_setopt(uploadContext->handle, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2_0);
    if (code != CURLE_OK) {
        fprintf(stderr, "__upload_block: failed to http version 2 [%s]\n", chef_error_buffer());
        goto cleanup;
    }

    code = curl_easy_setopt(uploadContext->handle, CURLOPT_HTTPHEADER, headers);
    if (code != CURLE_OK) {
        fprintf(stderr, "__upload_block: failed to set http headers [%s]\n", chef_error_buffer());
        goto cleanup;
    }

    code = curl_easy_setopt(uploadContext->handle, CURLOPT_PIPEWAIT, 1L);
    if (code != CURLE_OK) {
        fprintf(stderr, "__upload_block: failed to set pipe wait [%s]\n", chef_error_buffer());
        goto cleanup;
    }

    code = curl_easy_setopt(uploadContext->handle, CURLOPT_URL, context->url);
    if (code != CURLE_OK) {
        fprintf(stderr, "__upload_block: failed to set url [%s]\n", chef_error_buffer());
        goto cleanup;
    }

    status = 0;

cleanup:
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
    long               httpCode;

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

    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
    if (httpCode != 200) {
        status = -1;
        
        if (httpCode == 302) {
            status = -EACCES;
        }
        else {
            fprintf(stderr, "__commit_request: http error %ld [%s]\n", httpCode, chef_response_buffer());
            status = -EIO;
        }
        goto cleanup;
    }

    status = 0;

cleanup:
    free(body);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return status;
}

static int __upload_blocks(struct pack_response* response, struct file_upload_context* uploadContexts, int uploadCount)
{
    struct curl_slist* headers = NULL;
    CURLM*    transfersHandle;
    CURLMcode code;
    int       stillRunning;

    // set required ms headers
    headers = curl_slist_append(headers, "x-ms-blob-type: BlockBlob");
    headers = curl_slist_append(headers, "x-ms-blob-content-type: application/octet-stream");

    transfersHandle = curl_multi_init();
    for(int i = 0; i < uploadCount; i++) {
        __upload_block(headers, &uploadContexts[i], i);
        curl_multi_add_handle(transfersHandle, uploadContexts[i].handle);
    }

    curl_multi_setopt(transfersHandle, CURLMOPT_PIPELINING, CURLPIPE_MULTIPLEX);

    // We do HTTP/2 so let's stick to one connection per host
    curl_multi_setopt(transfersHandle, CURLMOPT_MAX_HOST_CONNECTIONS, 1L);

    do {
        code = curl_multi_perform(transfersHandle, &stillRunning);
        if (stillRunning) {
            code = curl_multi_poll(transfersHandle, NULL, 0, 1000, NULL);
        }

        if (code) {
            break;
        }
    } while(stillRunning);

    curl_multi_cleanup(transfersHandle);

    for (int i = 0; i < uploadCount; i++) {
        curl_multi_remove_handle(transfersHandle, uploadContexts[i].handle);
        curl_easy_cleanup(uploadContexts[i].handle);
    }

    curl_slist_free_all(headers);
}

int chefclient_pack_publish(struct chef_publish_params* params, const char* path)
{
    struct file_upload_context* uploadContexts;
    struct pack_response        context;
    json_t*                     request;
    int                         uploadCount;
    int                         status;

    // create all the neccessary contexts
    status = __create_file_contexts(path, &uploadContexts, &uploadCount);
    if (status != 0) {
        fprintf(stderr, "chefclient_pack_publish: failed to create file contexts\n");
        return status;
    }

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

    // now upload the blocks
    status = __upload_blocks(&context, uploadContexts, uploadCount);
    if (status != 0) {
        fprintf(stderr, "chefclient_pack_publish: failed to upload file\n");
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
