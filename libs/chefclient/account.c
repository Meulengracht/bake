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

#include <chef/client.h>
#include <chef/platform.h>
#include <chef/api/account.h>
#include <curl/curl.h>
#include <errno.h>
#include <jansson.h>
#include "private.h"
#include <string.h>
#include <vlog.h>

extern const char* chefclient_api_base_url(void);

struct chef_account_publisher {
    const char*                       name;
    enum chef_account_verified_status verified_status;
};

struct chef_account_apikey {
    const char* name;
};

struct chef_account {
    const char*              name;
    const char*              email;
    enum chef_account_status status;

    struct chef_account_publisher* publishers;
    size_t                         publisher_count;

    struct chef_account_apikey* api_keys;
    size_t                      api_keys_count;
};

static int __get_account_apikeys_url(char* urlBuffer, size_t bufferSize)
{
    int written = snprintf(urlBuffer, bufferSize - 1, 
        "%s/account/api-keys",
        chefclient_api_base_url()
    );
    return written < (bufferSize - 1) ? 0 : -1;
}

static int __get_account_publisher_url(char* urlBuffer, size_t bufferSize)
{
    int written = snprintf(urlBuffer, bufferSize - 1, 
        "%s/account/publishers",
        chefclient_api_base_url()
    );
    return written < (bufferSize - 1) ? 0 : -1;
}

static int __get_account_url(char* urlBuffer, size_t bufferSize)
{
    int written = snprintf(urlBuffer, bufferSize - 1, 
        "%s/account/me",
        chefclient_api_base_url()
    );
    return written < (bufferSize - 1) ? 0 : -1;
}

static json_t* __serialize_account(struct chef_account* account)
{
    json_t* json = json_object();
    if (json == NULL) {
        errno = ENOMEM;
        return NULL;
    }

    json_object_set_new(json, "name", json_string(account->name));
    return json;
}

static int __parse_account(const char* response, struct chef_account** accountOut)
{
    struct chef_account* account;
    json_error_t         error;
    json_t*              root;
    json_t*              member;

    root = json_loads(response, 0, &error);
    if (!root) {
        return -1;
    }

    // allocate memory for the account
    account = chef_account_new();
    if (account == NULL) {
        return -1;
    }

    // parse the account
    member = json_object_get(root, "name");
    if (member) {
        account->name = platform_strdup(json_string_value(member));
    }

    member = json_object_get(root, "email");
    if (member) {
        account->email = platform_strdup(json_string_value(member));
    }

    member = json_object_get(root, "status");
    if (member) {
        account->status = (enum chef_account_status)json_integer_value(member);
    }

    member = json_object_get(root, "publishers");
    if (member && json_array_size(member) > 0) {
        size_t count = json_array_size(member);
        
        account->publishers = (struct chef_account_publisher*)calloc(count, sizeof(struct chef_account_publisher));
        if (account->publishers == NULL) {
            chef_account_free(account);
            json_decref(root);
            return -1;
        }
        account->publisher_count = count;

        for (size_t i = 0; i < count; i++) {
            json_t* publisher = json_array_get(member, i);
            account->publishers[i].name = platform_strdup(json_string_value(json_object_get(publisher, "name")));
            account->publishers[i].verified_status = (enum chef_account_verified_status)json_integer_value(json_object_get(publisher, "status"));
        }
    }

    *accountOut = account;

    json_decref(root);
    return 0;
}

int __get_account(struct chef_account** accountOut)
{
    struct chef_request* request;
    CURLcode             code;
    char                 buffer[256];
    int                  status = -1;
    long                 httpCode;

    request = chef_request_new(CHEF_CLIENT_API_SECURE, 1);
    if (!request) {
        VLOG_ERROR("chef-client", "__get_account: failed to create request\n");
        return -1;
    }

    // set the url
    if (__get_account_url(buffer, sizeof(buffer)) != 0) {
        VLOG_ERROR("chef-client", "__get_account: buffer too small for account link\n");
        goto cleanup;
    }

    code = curl_easy_setopt(request->curl, CURLOPT_URL, &buffer[0]);
    if (code != CURLE_OK) {
        VLOG_ERROR("chef-client", "__get_account: failed to set url [%s]\n", request->error);
        goto cleanup;
    }

    code = chef_request_execute(request);
    if (code != CURLE_OK) {
        VLOG_ERROR("chef-client", "__get_account: chef_request_execute() failed: %s\n", curl_easy_strerror(code));
    }

    curl_easy_getinfo(request->curl, CURLINFO_RESPONSE_CODE, &httpCode);
    if (httpCode != 200) {
        if (httpCode == 404) {
            status = -ENOENT;
        }
        else if (httpCode == 302) {
            status = -EACCES;
        }
        else {
            VLOG_ERROR("chef-client", "__get_account: http error %ld\n", httpCode);
            status = -EIO;
        }
        goto cleanup;
    }

    status = __parse_account(request->response, accountOut);

cleanup:
    chef_request_delete(request);
    return status;
}

static int __update_account(json_t* json, struct chef_account** accountOut)
{
    struct chef_request* request;
    CURLcode             code;
    char*                body   = NULL;
    int                  status = -1;
    char                 buffer[256];
    long                 httpCode;

    request = chef_request_new(CHEF_CLIENT_API_SECURE, 1);
    if (!request) {
        VLOG_ERROR("chef-client", "__update_account: failed to create request\n");
        return -1;
    }

    // set the url
    if (__get_account_url(buffer, sizeof(buffer)) != 0) {
        VLOG_ERROR("chef-client", "__update_account: buffer too small for account link\n");
        goto cleanup;
    }

    code = curl_easy_setopt(request->curl, CURLOPT_URL, &buffer[0]);
    if (code != CURLE_OK) {
        VLOG_ERROR("chef-client", "__update_account: failed to set url [%s]\n", request->error);
        goto cleanup;
    }

    body = json_dumps(json, 0);
    code = curl_easy_setopt(request->curl, CURLOPT_POSTFIELDS, body);
    if (code != CURLE_OK) {
        VLOG_ERROR("chef-client", "__update_account: failed to set body [%s]\n", request->error);
        goto cleanup;
    }

    code = chef_request_execute(request);
    if (code != CURLE_OK) {
        VLOG_ERROR("chef-client", "__update_account: chef_request_execute() failed: %s\n", curl_easy_strerror(code));
    }

    curl_easy_getinfo(request->curl, CURLINFO_RESPONSE_CODE, &httpCode);
    if (httpCode < 200 || httpCode >= 300) {
        status = -1;
        if (httpCode == 302) {
            status = -EACCES;
        } else {
            VLOG_ERROR("chef-client", "__update_account: http error %ld [%s]\n", httpCode, request->response);
            status = -EIO;
        }
        goto cleanup;
    } else {
        status = 0;
    }

    if (accountOut != NULL) {
        status = __parse_account(request->response, accountOut);
    }

cleanup:
    free(body);
    chef_request_delete(request);
    return status;
}

int chef_account_get(struct chef_account** accountOut)
{
    return __get_account(accountOut);
}

int chef_account_update(struct chef_account* account)
{
    json_t* json;
    int     status;

    if (account == NULL) {
        errno = EINVAL;
        return -1;
    }

    json = __serialize_account(account);
    if (json == NULL) {
        return -1;
    }

    status = __update_account(json, NULL);

    json_decref(json);
    return status;
}

static int __parse_publisher_register_response(const char* response)
{
    json_error_t error;
    json_t*      root;
    json_t*      member;

    root = json_loads(response, 0, &error);
    if (!root) {
        return -1;
    }

    // root is 
    // message => string
    // publisherId
    member = json_object_get(root, "message");
    if (member != NULL) {
        printf("%s\n", json_string_value(member));
    }

    json_decref(root);
    return 0;
}

int chef_account_publisher_register(const char* name, const char* email)
{
    struct chef_request* request;
    CURLcode             code;
    int                  status = -1;
    char                 buffer[256];
    char                 body[1024];
    long                 httpCode;

    request = chef_request_new(CHEF_CLIENT_API_SECURE, 1);
    if (!request) {
        VLOG_ERROR("chef-client", "chef_account_publisher_register: failed to create request\n");
        return -1;
    }

    // set the url
    if (__get_account_publisher_url(buffer, sizeof(buffer)) != 0) {
        VLOG_ERROR("chef-client", "chef_account_publisher_register: buffer too small for account link\n");
        goto cleanup;
    }

    code = curl_easy_setopt(request->curl, CURLOPT_URL, &buffer[0]);
    if (code != CURLE_OK) {
        VLOG_ERROR("chef-client", "chef_account_publisher_register: failed to set url [%s]\n", request->error);
        goto cleanup;
    }

    // format the json body
    snprintf(
        &body[0],
        sizeof(body),
        "{\"PublisherName\":\"%s\",\"PublisherEmail\":\"%s\"}",
        name, email
    );

    code = curl_easy_setopt(request->curl, CURLOPT_POSTFIELDS, body);
    if (code != CURLE_OK) {
        VLOG_ERROR("chef-client", "chef_account_publisher_register: failed to set body [%s]\n", request->error);
        goto cleanup;
    }

    code = chef_request_execute(request);
    if (code != CURLE_OK) {
        VLOG_ERROR("chef-client", "chef_account_publisher_register: chef_request_execute() failed: %s\n", curl_easy_strerror(code));
    }

    curl_easy_getinfo(request->curl, CURLINFO_RESPONSE_CODE, &httpCode);
    if (httpCode < 200 || httpCode >= 300) {
        status = -1;
        if (httpCode == 302) {
            status = -EACCES;
        } else {
            VLOG_ERROR("chef-client", "chef_account_publisher_register: http error %ld [%s]\n", httpCode, request->response);
            status = -EIO;
        }
        goto cleanup;
    } else {
        status = 0;
    }

    status = __parse_publisher_register_response(request->response);

cleanup:
    chef_request_delete(request);
    return status;
}

static int __parse_apikeys_create_response(const char* response, char** apiKey)
{
    json_error_t error;
    json_t*      root;
    json_t*      member;

    root = json_loads(response, 0, &error);
    if (!root) {
        return -1;
    }

    // root is
    // key => string
    member = json_object_get(root, "key");
    if (member != NULL) {
        *apiKey = platform_strdup(json_string_value(member));
    }

    json_decref(root);
    return 0;
}

int chef_account_apikey_create(const char* name, char** apiKey)
{
    struct chef_request* request;
    CURLcode             code;
    int                  status = -1;
    char                 buffer[256];
    char                 body[1024];
    long                 httpCode;

    request = chef_request_new(CHEF_CLIENT_API_SECURE, 1);
    if (!request) {
        VLOG_ERROR("chef-client", "chef_account_publisher_register: failed to create request\n");
        return -1;
    }

    // set the url
    if (__get_account_apikeys_url(buffer, sizeof(buffer)) != 0) {
        VLOG_ERROR("chef-client", "chef_account_publisher_register: buffer too small for account link\n");
        goto cleanup;
    }

    code = curl_easy_setopt(request->curl, CURLOPT_URL, &buffer[0]);
    if (code != CURLE_OK) {
        VLOG_ERROR("chef-client", "chef_account_publisher_register: failed to set url [%s]\n", request->error);
        goto cleanup;
    }

    // format the json body
    snprintf(
        &body[0],
        sizeof(body),
        "{\"Name\":\"%s\"}",
        name
    );

    code = curl_easy_setopt(request->curl, CURLOPT_POSTFIELDS, body);
    if (code != CURLE_OK) {
        VLOG_ERROR("chef-client", "chef_account_publisher_register: failed to set body [%s]\n", request->error);
        goto cleanup;
    }

    code = chef_request_execute(request);
    if (code != CURLE_OK) {
        VLOG_ERROR("chef-client", "chef_account_publisher_register: chef_request_execute() failed: %s\n", curl_easy_strerror(code));
    }

    curl_easy_getinfo(request->curl, CURLINFO_RESPONSE_CODE, &httpCode);
    if (httpCode < 200 || httpCode >= 300) {
        status = -1;
        if (httpCode == 302) {
            status = -EACCES;
        } else {
            VLOG_ERROR("chef-client", "chef_account_publisher_register: http error %ld [%s]\n", httpCode, request->response);
            status = -EIO;
        }
        goto cleanup;
    } else {
        status = 0;
    }

    status = __parse_apikeys_create_response(request->response, apiKey);

cleanup:
    chef_request_delete(request);
    return status;
}

int chef_account_apikey_delete(const char* name)
{
    struct chef_request* request;
    CURLcode             code;
    int                  status = -1;
    char                 buffer[256];
    char                 body[1024];
    long                 httpCode;

    request = chef_request_new(CHEF_CLIENT_API_SECURE, 1);
    if (!request) {
        VLOG_ERROR("chef-client", "chef_account_apikey_delete: failed to create request\n");
        return -1;
    }

    // set the url
    if (__get_account_apikeys_url(buffer, sizeof(buffer)) != 0) {
        VLOG_ERROR("chef-client", "chef_account_apikey_delete: buffer too small for account link\n");
        goto cleanup;
    }

    code = curl_easy_setopt(request->curl, CURLOPT_URL, &buffer[0]);
    if (code != CURLE_OK) {
        VLOG_ERROR("chef-client", "chef_account_apikey_delete: failed to set url [%s]\n", request->error);
        goto cleanup;
    }

    // format the json body
    snprintf(
        &body[0],
        sizeof(body),
        "{\"Name\":\"%s\"}",
        name
    );

    code = curl_easy_setopt(request->curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    if (code != CURLE_OK) {
        VLOG_ERROR("chef-client", "chef_account_apikey_delete: failed to set delete option [%s]\n", request->error);
        goto cleanup;
    }

    code = curl_easy_setopt(request->curl, CURLOPT_POSTFIELDS, body);
    if (code != CURLE_OK) {
        VLOG_ERROR("chef-client", "chef_account_apikey_delete: failed to set body [%s]\n", request->error);
        goto cleanup;
    }

    code = chef_request_execute(request);
    if (code != CURLE_OK) {
        VLOG_ERROR("chef-client", "chef_account_apikey_delete: chef_request_execute() failed: %s\n", curl_easy_strerror(code));
    }

    curl_easy_getinfo(request->curl, CURLINFO_RESPONSE_CODE, &httpCode);
    if (httpCode < 200 || httpCode >= 300) {
        status = -1;
        if (httpCode == 302) {
            status = -EACCES;
        } else {
            VLOG_ERROR("chef-client", "chef_account_apikey_delete: http error %ld [%s]\n", httpCode, request->response);
            status = -EIO;
        }
        goto cleanup;
    } else {
        status = 0;
    }

cleanup:
    chef_request_delete(request);
    return status;
}

struct chef_account* chef_account_new(void)
{
    struct chef_account* account = (struct chef_account*)malloc(sizeof(struct chef_account));
    if (account == NULL) {
        errno = ENOMEM;
        return NULL;
    }

    memset(account, 0, sizeof(struct chef_account));
    return account;
}

void chef_account_free(struct chef_account* account)
{
    if (account == NULL) {
        errno = EINVAL;
        return;
    }

    free((void*)account->name);
    free(account);
}


const char* chef_account_get_name(struct chef_account* account)
{
    if (account == NULL) {
        errno = EINVAL;
        return NULL;
    }
    return account->name;
}

const char* chef_account_get_email(struct chef_account* account)
{
    if (account == NULL) {
        errno = EINVAL;
        return NULL;
    }
    return account->email;
}

enum chef_account_status chef_account_get_status(struct chef_account* account)
{
    if (account == NULL) {
        errno = EINVAL;
        return CHEF_ACCOUNT_STATUS_UNKNOWN;
    }
    return account->status;
}

void chef_account_set_name(struct chef_account* account, const char* name)
{
    if (account == NULL) {
        errno = EINVAL;
        return;
    }

    if (account->name != NULL) {
        free((void*)account->name);
    }

    account->name = platform_strdup(name);
}

int chef_account_get_publisher_count(struct chef_account* account)
{
    if (account == NULL) {
        errno = EINVAL;
        return -1;
    }
    return (int)account->publisher_count;
}

const char* chef_account_get_publisher_name(struct chef_account* account, int index)
{
    if (account == NULL || index < 0 || (size_t)index >= account->publisher_count) {
        errno = EINVAL;
        return NULL;
    }
    return account->publishers[index].name;
}

enum chef_account_verified_status chef_account_get_publisher_verified_status(struct chef_account* account, int index)
{
    if (account == NULL || index < 0 || (size_t)index >= account->publisher_count) {
        errno = EINVAL;
        return CHEF_ACCOUNT_VERIFIED_STATUS_UNKNOWN;
    }
    return account->publishers[index].verified_status;
}

int chef_account_get_apikey_count(struct chef_account* account)
{
    if (account == NULL) {
        errno = EINVAL;
        return -1;
    }
    return (int)account->api_keys_count;
}

const char* chef_account_get_apikey_name(struct chef_account* account, int index)
{
    if (account == NULL || index < 0 || (size_t)index >= account->api_keys_count) {
        errno = EINVAL;
        return NULL;
    }
    return account->api_keys[index].name;
}
