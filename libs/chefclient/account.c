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

struct chef_account {
    const char* publisher_name;
    const char* publisher_email;

    enum chef_account_status          status;
    enum chef_account_verified_status verified_status;
};

static int __get_account_url(char* urlBuffer, size_t bufferSize)
{
    int written = snprintf(urlBuffer, bufferSize - 1, 
        "%s/account",
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

    json_object_set_new(json, "publisher-name", json_string(account->publisher_name));
    json_object_set_new(json, "publisher-email", json_string(account->publisher_email));
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
    member = json_object_get(root, "publisher-name");
    if (member) {
        account->publisher_name = platform_strdup(json_string_value(member));
    }

    member = json_object_get(root, "publisher-email");
    if (member) {
        account->publisher_email = platform_strdup(json_string_value(member));
    }

    member = json_object_get(root, "status");
    if (member) {
        account->status = (enum chef_account_status)json_integer_value(member);
    }

    member = json_object_get(root, "verification-status");
    if (member) {
        account->verified_status = (enum chef_account_verified_status)json_integer_value(member);
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

    request = chef_request_new(1, 1);
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

    code = curl_easy_perform(request->curl);
    if (code != CURLE_OK) {
        VLOG_ERROR("chef-client", "__get_account: curl_easy_perform() failed: %s\n", curl_easy_strerror(code));
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
    char*                body      = NULL;
    int                  status    = -1;
    char                 buffer[256];
    long                 httpCode;

    request = chef_request_new(1, 1);
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

    code = curl_easy_perform(request->curl);
    if (code != CURLE_OK) {
        VLOG_ERROR("chef-client", "__update_account: curl_easy_perform() failed: %s\n", curl_easy_strerror(code));
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

    free((void*)account->publisher_name);
    free(account);
}

const char* chef_account_get_publisher_name(struct chef_account* account)
{
    if (account == NULL) {
        errno = EINVAL;
        return NULL;
    }

    return account->publisher_name;
}

void chef_account_set_publisher_name(struct chef_account* account, const char* publisherName)
{
    if (account == NULL) {
        errno = EINVAL;
        return;
    }

    if (account->publisher_name != NULL) {
        free((void*)account->publisher_name);
    }

    account->publisher_name = platform_strdup(publisherName);
}

void chef_account_set_publisher_email(struct chef_account* account, const char* publisherEmail)
{
    if (account == NULL) {
        errno = EINVAL;
        return;
    }

    if (account->publisher_email != NULL) {
        free((void*)account->publisher_email);
    }

    account->publisher_email = platform_strdup(publisherEmail);
}

const char* chef_account_get_publisher_email(struct chef_account* account)
{
    if (account == NULL) {
        errno = EINVAL;
        return NULL;
    }

    return account->publisher_email;
}

enum chef_account_status chef_account_get_status(struct chef_account* account)
{
    if (account == NULL) {
        errno = EINVAL;
        return CHEF_ACCOUNT_STATUS_UNKNOWN;
    }

    return account->status;
}

enum chef_account_verified_status chef_account_get_verified_status(struct chef_account* account)
{
    if (account == NULL) {
        errno = EINVAL;
        return CHEF_ACCOUNT_VERIFIED_STATUS_UNKNOWN;
    }

    return account->verified_status;
}
