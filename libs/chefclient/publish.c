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
#include "base64/base64.h"
#include <stdio.h>
#include <string.h>

// Max supported by REST api on azure is 100mb
#define CHEF_UPLOAD_MAX_SIZE (100 * 1024 * 1024)

struct pack_response {
    const char* token;
    const char* url;
};

struct file_upload_context {
    char*  block_id;
    CURL*  handle;
    FILE*  file;
    size_t length;
    size_t bytes_read;
    size_t bytes_uploaded;
};

static const char* g_templateGuid = "xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx";
static const char* g_hexValues    = "0123456789ABCDEF-";

static void __generate_bad_but_valid_guid(char guidBuffer[40])
{
    int templateLength = strlen(g_templateGuid);

    // yes we are well aware that this provides _very_ poor randomness
    // but we do not require a cryptographically secure guid for this purpose
    srand(clock());
    for (int t = 0; t < templateLength + 1; t++) {
        int  r = rand() % 16;
        char c = ' ';

        switch (g_templateGuid[t]) {
            case 'x' : { c = g_hexValues[r]; } break;
            case 'y' : { c = g_hexValues[(r & 0x03) | 0x08]; } break;
            case '-' : { c = '-'; } break;
            case '4' : { c = '4'; } break;
        }

        guidBuffer[t] = (t < templateLength) ? c : 0x00;
    }
}

static char* __generate_blockid(void)
{
    char guidBuffer[40] = { 0 };

    // generate a guid for the block id
    __generate_bad_but_valid_guid(guidBuffer);
    
    // encode it in base64
    return (char*)base64_encode(guidBuffer, strlen(guidBuffer), NULL);
}

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

        contexts[i].block_id = __generate_blockid();
        contexts[i].length = (i == (contextCount - 1)) ? (fileSize - (i * CHEF_UPLOAD_MAX_SIZE)) : CHEF_UPLOAD_MAX_SIZE;
        contexts[i].bytes_read = 0;
        contexts[i].bytes_uploaded = 0;
        contexts[i].handle = NULL;
    }

    *contextsOut = contexts;
    *contextCountOut = contextCount;
    return 0;
}


static void __destroy_file_contexts(struct file_upload_context* contexts, int contextCount)
{
    for (int i = 0; i < contextCount; i++) {
        if (contexts[i].file) {
            fclose(contexts[i].file);
        }
        if (contexts[i].block_id) {
            free(contexts[i].block_id);
        }
    }
    free(contexts);
}

static int __upload_progress_callback(void *clientp,
    double dltotal, double dlnow,
    double ultotal, double ulnow)
{
    struct file_upload_context* context = (struct file_upload_context*)clientp;
    context->bytes_uploaded = (size_t)ulnow;
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

static char* __create_blocklist_request(struct file_upload_context* contexts, int contextCount)
{
    char* request;

    request = malloc(4096);
    if (!request) {
        return NULL;
    }

    strcpy(request, "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n");
    strcat(request, "<BlockList>\n");
    for (int i = 0; i < contextCount; i++) {
        strcat(request, "  <Latest>");
        strcat(request, contexts[i].block_id);
        strcat(request, "</Latest>\n");
    }
    strcat(request, "</BlockList>\n");
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

static int __get_block_url(char* urlBuffer, size_t bufferSize, const char* urlBase, const char* blockId)
{
    int written = snprintf(urlBuffer, bufferSize - 1, 
        "%s&comp=block&blockid=%s",
        urlBase,
        blockId
    );
    urlBuffer[written] = '\0';
    return written == bufferSize - 1 ? -1 : 0;
}

static int __get_blocklist_url(char* urlBuffer, size_t bufferSize, const char* urlBase)
{
    int written = snprintf(urlBuffer, bufferSize - 1, 
        "%s&comp=blocklist",
        urlBase
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

static int __write_blocklist(
    struct pack_response*       context,
    struct file_upload_context* fileContexts,
    int                         contextCount)
{
    CURL*              curl;
    CURLcode           code;
    struct curl_slist* headers   = NULL;
    char*              body      = NULL;
    int                status    = -1;
    char               buffer[512];
    long               httpCode;

    // initialize a curl session
    curl = curl_easy_init();
    if (!curl) {
        fprintf(stderr, "__write_blocklist: curl_easy_init() failed\n");
        return -1;
    }
    chef_set_curl_common(curl, (void**)&headers, 0, 1, 0);

    // set required ms headers
    headers = curl_slist_append(headers, "x-ms-version: 2016-05-31");

    // set the url
    if (__get_blocklist_url(buffer, sizeof(buffer), context->url) != 0) {
        fprintf(stderr, "__write_blocklist: buffer too small for publish link\n");
        goto cleanup;
    }

    code = curl_easy_setopt(curl, CURLOPT_URL, &buffer[0]);
    if (code != CURLE_OK) {
        fprintf(stderr, "__write_blocklist: failed to set url [%s]\n", chef_error_buffer());
        goto cleanup;
    }

    code = curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
    if(code != CURLE_OK) {
        fprintf(stderr, "__write_blocklist: failed to mark request PUT [%s]\n", chef_error_buffer());
        goto cleanup;
    }
    
    body = __create_blocklist_request(fileContexts, contextCount);
    code = curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
    if (code != CURLE_OK) {
        fprintf(stderr, "__write_blocklist: failed to set body [%s]\n", chef_error_buffer());
        goto cleanup;
    }

    code = curl_easy_perform(curl);
    if (code != CURLE_OK) {
        fprintf(stderr, "__write_blocklist: curl_easy_perform() failed: %s\n", curl_easy_strerror(code));
    }

    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
    if (httpCode < 200 || httpCode >= 300) {
        status = -1;
        
        if (httpCode == 302) {
            status = -EACCES;
        }
        else {
            fprintf(stderr, "__write_blocklist: http error %ld\n", httpCode);
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

static int __prepare_block_request(
    struct curl_slist*          headers,
    struct file_upload_context* fileContext,
    struct pack_response*       context)
{
    CURLcode code;
    int      status = -1;
    char     urlBuffer[512];

    // initialize a curl session
    fileContext->handle = curl_easy_init();
    if (!fileContext->handle) {
        fprintf(stderr, "__prepare_block_request: curl_easy_init() failed\n");
        return -1;
    }
    chef_set_curl_common(fileContext->handle, NULL, 0, 1, 0);

    code = curl_easy_setopt(fileContext->handle, CURLOPT_READFUNCTION, __read_file);
    if (code != CURLE_OK) {
        fprintf(stderr, "__prepare_block_request: failed to set upload data reader [%s]\n", chef_error_buffer());
        goto cleanup;
    }

    code = curl_easy_setopt(fileContext->handle, CURLOPT_READDATA, fileContext);
    if (code != CURLE_OK) {
        fprintf(stderr, "__prepare_block_request: failed to set upload data [%s]\n", chef_error_buffer());
        goto cleanup;
    }

    code = curl_easy_setopt(fileContext->handle, CURLOPT_INFILESIZE_LARGE, fileContext->length);
    if (code != CURLE_OK) {
        fprintf(stderr, "__prepare_block_request: failed to set upload size [%s]\n", chef_error_buffer());
        goto cleanup;
    }

    code = curl_easy_setopt(fileContext->handle, CURLOPT_UPLOAD, 1L);
    if (code != CURLE_OK) {
        fprintf(stderr, "__prepare_block_request: failed to set request to upload [%s]\n", chef_error_buffer());
        goto cleanup;
    }

    code = curl_easy_setopt(fileContext->handle, CURLOPT_NOPROGRESS, 0);
    if (code != CURLE_OK) {
        fprintf(stderr, "__prepare_block_request: failed to enable upload progress [%s]\n", chef_error_buffer());
        goto cleanup;
    }

    code = curl_easy_setopt(fileContext->handle, CURLOPT_PROGRESSFUNCTION, __upload_progress_callback);
    if (code != CURLE_OK) {
        fprintf(stderr, "__prepare_block_request: failed to set upload progress callback [%s]\n", chef_error_buffer());
        goto cleanup;
    }

    code = curl_easy_setopt(fileContext->handle, CURLOPT_PROGRESSDATA, fileContext);
    if (code != CURLE_OK) {
        fprintf(stderr, "__prepare_block_request: failed to set upload progress callback data [%s]\n", chef_error_buffer());
        goto cleanup;
    }

    code = curl_easy_setopt(fileContext->handle, CURLOPT_HTTPHEADER, headers);
    if (code != CURLE_OK) {
        fprintf(stderr, "__prepare_block_request: failed to set http headers [%s]\n", chef_error_buffer());
        goto cleanup;
    }

    if (__get_block_url(urlBuffer, sizeof(urlBuffer), context->url, fileContext->block_id) != 0) {
        fprintf(stderr, "__prepare_block_request: failed to get block url [%s]\n", chef_error_buffer());
        goto cleanup;
    }

    code = curl_easy_setopt(fileContext->handle, CURLOPT_URL, urlBuffer);
    if (code != CURLE_OK) {
        fprintf(stderr, "__prepare_block_request: failed to set url [%s]\n", chef_error_buffer());
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

static int __update_progress(struct file_upload_context* uploadContexts, int uploadCount)
{
    int    status = 0;
    size_t total = 0;
    size_t complete = 0;
    int    percent = 0;

    for (int i = 0; i < uploadCount; i++) {
        total    += uploadContexts[i].length;
        complete += uploadContexts[i].bytes_uploaded;
    }

    percent = (complete * 100) / total;
    
    // print a fancy progress bar with percentage, upload progress and a moving
    // bar being filled
    printf("\r[");
    for (int i = 0; i < 20; i++) {
        if (i < percent / 5) {
            printf("#");
        }
        else {
            printf(" ");
        }
    }
    printf("| %3d%%] %6zu / %6zu bytes", percent, complete, total);
    fflush(stdout);
    return status;
}

static int __upload_blocks(struct pack_response* response, struct file_upload_context* uploadContexts, int uploadCount)
{
    struct curl_slist* headers = NULL;
    CURLM*    transfersHandle;
    CURLMcode code;
    int       stillRunning;
    int       status = 0;

    // set required ms headers
    headers = curl_slist_append(headers, "x-ms-version: 2016-05-31");

    transfersHandle = curl_multi_init();
    for(int i = 0; i < uploadCount; i++) {
        status = __prepare_block_request(headers, &uploadContexts[i], response);
        if (status != 0) {
            fprintf(stderr, "__upload_blocks: failed to prepare block request [%s]\n", uploadContexts[i].block_id);
            goto cleanup;
        }
        
        code = curl_multi_add_handle(transfersHandle, uploadContexts[i].handle);
        if (code != CURLM_OK) {
            fprintf(stderr, "__upload_blocks: failed to add handle [%s]\n", curl_multi_strerror(code));
            goto cleanup;
        }
    }

    curl_multi_setopt(transfersHandle, CURLMOPT_MAXCONNECTS, (long)10);

    do {
        code = curl_multi_perform(transfersHandle, &stillRunning);
        __update_progress(uploadContexts, uploadCount);

        if (stillRunning) {
            code = curl_multi_poll(transfersHandle, NULL, 0, 1000, NULL);
        }
    } while(stillRunning && code == CURLM_OK);
    printf("\n");
    if (code != CURLM_OK) {
        fprintf(stderr, "__upload_blocks: curl_multi_perform() failed: %s\n", curl_multi_strerror(code));
        status = -1;
    }

cleanup:
    curl_multi_cleanup(transfersHandle);
    for (int i = 0; i < uploadCount; i++) {
        if (uploadContexts[i].handle) {
            curl_multi_remove_handle(transfersHandle, uploadContexts[i].handle);
            curl_easy_cleanup(uploadContexts[i].handle);
        }
    }
    curl_slist_free_all(headers);
    if (status != 0) {
        return -1;
    }

    // lastly, we would like to commit the block list
    return __write_blocklist(response, uploadContexts, uploadCount);
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
        __destroy_file_contexts(uploadContexts, uploadCount);
        fprintf(stderr, "chefclient_pack_publish: failed to create publish request\n");
        return -1;
    }

    // build and execute the curl request
    status = __publish_request(request, &context);

    // cleanup request
    json_decref(request);

    if (status != 0) {
        __destroy_file_contexts(uploadContexts, uploadCount);
        fprintf(stderr, "chefclient_pack_publish: failed to publish pack\n");
        return -1;
    }

    // now upload the blocks
    status = __upload_blocks(&context, uploadContexts, uploadCount);

    // cleanup file contexts
    __destroy_file_contexts(uploadContexts, uploadCount);

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
