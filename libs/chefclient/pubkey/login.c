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

#include "pubkey.h"
#include "../private.h"
#include <chef/platform.h>
#include <curl/curl.h>
#include <errno.h>
#include <jansson.h>
#include <openssl/pem.h>
#include <openssl/err.h>
#include <vlog.h>

#define __CHEF_ENDPOINT_LOGIN "https://api.chef.io/login"

struct __pubkey_context {
    char* account_guid;
    char* jwt_token;
};

// This is the message we will sign using the private key which
// is used to authenticate the user.
static const char*             g_message = "chef-is-an-awesome-tool";
static struct __pubkey_context g_context = { 0 };
static char                    g_bearer[1024];

static int __load_pubkey_settings(void)
{
    // do we have pubkey settings stored?
    if (chefclient_settings() != NULL) {
        json_t* pubkey = json_object_get(chefclient_settings(), "pubkey");
        if (pubkey != NULL) {
            json_t* accessToken  = json_object_get(pubkey, "access-token");
            json_t* refreshToken = json_object_get(pubkey, "refresh-token");

            if (json_string_value(refreshToken) != NULL && strlen(json_string_value(refreshToken)) > 0) {
                g_tokenContext.refresh_token = platform_strdup(json_string_value(refreshToken));
            }
            if (json_string_value(accessToken) != NULL && strlen(json_string_value(accessToken)) > 0) {
                g_tokenContext.access_token = platform_strdup(json_string_value(accessToken));
            }
            return 0;
        }
    }
    return -1;
}

static void __save_pubkey_settings(void)
{
    if (chefclient_settings() != NULL) {
        const char* empty = "";
        json_t*     pubkey;
        json_t*     accessToken;
        json_t*     refreshToken;

        pubkey = json_object();
        if (pubkey == NULL) {
            return;
        }

        accessToken = json_string(g_tokenContext.access_token);
        if (accessToken == NULL) {
            accessToken = json_string(empty);
        }

        refreshToken = json_string(g_tokenContext.refresh_token);
        if (refreshToken == NULL) {
            refreshToken = json_string(empty);
        }

        // build pubkey object
        json_object_set_new(pubkey, "access-token", accessToken);
        json_object_set_new(pubkey, "refresh-token", refreshToken);

        // store the pubkey object
        json_object_set_new(chefclient_settings(), "pubkey", pubkey);
    }
}

static int __pubkey_sign(const char* privateKey, char** signatureOut, size_t* siglenOut)
{
    unsigned char* sig;
    EVP_MD_CTX*    mdctx;
    size_t         siglen = 0;
    EVP_PKEY*      pkey = NULL;
    FILE*          fp;
    VLOG_DEBUG("chef-client", "__pubkey_sign(privateKey=%s)\n", privateKey);

    fp = fopen(privateKey, "r");
    if (fp == NULL) {
        VLOG_ERROR("chef-client", "pubkey_login: failed to open private key file %s: %s\n", privateKey, strerror(errno));
        return -1;
    }

    pkey = PEM_read_PrivateKey(fp, NULL, NULL, NULL);
    fclose(fp);
    if (pkey == NULL) {
        VLOG_ERROR("chef-client", "pubkey_login: failed to read private key from file %s: %s\n", privateKey, ERR_error_string(ERR_get_error(), NULL));
        return -1;
    }

    mdctx = EVP_MD_CTX_new();
    if (mdctx == NULL) {
        VLOG_ERROR("chef-client", "pubkey_login: failed to create EVP_MD_CTX: %s\n", ERR_error_string(ERR_get_error(), NULL));
        EVP_PKEY_free(pkey);
        return -1;
    }

    if (EVP_DigestSignInit(mdctx, NULL, EVP_sha256(), NULL, pkey) <= 0) {
        VLOG_ERROR("chef-client", "pubkey_login: failed to initialize digest sign: %s\n", ERR_error_string(ERR_get_error(), NULL));
        EVP_MD_CTX_free(mdctx);
        EVP_PKEY_free(pkey);
        return -1;
    }

    if (EVP_DigestSignUpdate(mdctx, g_message, strlen(g_message)) <= 0) {
        VLOG_ERROR("chef-client", "pubkey_login: failed to update digest sign: %s\n", ERR_error_string(ERR_get_error(), NULL));
        EVP_MD_CTX_free(mdctx);
        EVP_PKEY_free(pkey);
        return -1;
    }

    if (EVP_DigestSignFinal(mdctx, NULL, &siglen) <= 0) {
        VLOG_ERROR("chef-client", "pubkey_login: failed to finalize digest sign: %s\n", ERR_error_string(ERR_get_error(), NULL));
        EVP_MD_CTX_free(mdctx);
        EVP_PKEY_free(pkey);
        return -1;
    }

    sig = malloc(siglen);
    if (sig == NULL) {
        VLOG_ERROR("chef-client", "pubkey_login: failed to allocate memory for signature\n");
        EVP_MD_CTX_free(mdctx);
        EVP_PKEY_free(pkey);
        return -1;
    }

    if (EVP_DigestSignFinal(mdctx, sig, &siglen) <= 0) {
        VLOG_ERROR("chef-client", "pubkey_login: failed to finalize digest sign with signature: %s\n", ERR_error_string(ERR_get_error(), NULL));
        free(sig);
        EVP_MD_CTX_free(mdctx);
        EVP_PKEY_free(pkey);
        return -1;
    }
    EVP_MD_CTX_free(mdctx);
    EVP_PKEY_free(pkey);

    VLOG_DEBUG("chef-client", "pubkey_login: successfully signed message with private key\n");
    *signatureOut = (char*)sig;
    *siglenOut = siglen;
    return 0;
}

static int __parse_token_response(const char* responseBuffer, struct __pubkey_context* context)
{
    json_error_t error;
    json_t*      root;

    root = json_loads(responseBuffer, 0, &error);
    if (!root) {
        VLOG_ERROR("chef-client", "__parse_token_response: failed to parse json: %s\n", error.text);
        return -1;
    }

    // get rest of values that should be there
    context->account_guid = platform_strdup(json_string_value(json_object_get(root, "accountId")));
    context->jwt_token = platform_strdup(json_string_value(json_object_get(root, "token")));

    json_decref(root);
    return 0;
}

static int __pubkey_post_login(const char* publicKey, const char* signature, struct __pubkey_context* context)
{
    char                 buffer[1024];
    struct chef_request* request;
    CURLcode             code;
    long                 httpCode;
    int                  status = -1;
    VLOG_DEBUG("chef-client", "__pubkey_post_login(publicKey=%s, signature=%s)\n", publicKey, signature);

    request = chef_request_new(1, 0);
    if (!request) {
        VLOG_ERROR("chef-client", "__pubkey_post_login: failed to create request\n");
        return -1;
    }

    // Set the content type and accept headers
    request->headers = curl_slist_append(request->headers, "Content-Type: application/json");
    request->headers = curl_slist_append(request->headers, "Accept: application/json");

    // Set the URL and headers
    code = curl_easy_setopt(request->curl, CURLOPT_URL, __CHEF_ENDPOINT_LOGIN);
    if (code != CURLE_OK) {
        VLOG_ERROR("chef-client", "__pubkey_post_login: failed to set url [%s]\n", request->error);
        goto cleanup;
    }

    // Prepare the JSON payload
    snprintf(
        buffer,
        sizeof(buffer),
        "{\"Name\":\"%s\",\"Email\":\"%s\",\"PublicKey\":\"%s\",\"SecurityToken\":\"%s\"}",
        publicKey, signature
    );

    code = curl_easy_setopt(request->curl, CURLOPT_POSTFIELDS, &buffer[0]);
    if (code != CURLE_OK) {
        VLOG_ERROR("chef-client", "__pubkey_post_login: failed to set body [%s]\n", request->error);
        goto cleanup;
    }

    code = curl_easy_perform(request->curl);
    if (code != CURLE_OK) {
        VLOG_ERROR("chef-client", "__pubkey_post_login: curl_easy_perform() failed: %s\n", curl_easy_strerror(code));
    }

    curl_easy_getinfo(request->curl, CURLINFO_RESPONSE_CODE, &httpCode);
    if (httpCode != 200) {
        VLOG_ERROR("chef-client", "__pubkey_post_login: http error %ld [%s]\n", httpCode, request->response);
        status = -1;
        errno = EIO;
        goto cleanup;
    }

    status = __parse_token_response(request->response, context);

cleanup:
    chef_request_delete(request);
    return status;
}

int pubkey_login(const char* publicKey, const char* privateKey)
{
    char*  signature = NULL;
    size_t siglen = 0;
    int    status;

    // Use openSSL libraries to sign the message with the private key, and then
    // we use the signed message and our public key in the chef api headers to 
    // prove who we are.
    if (publicKey == NULL || privateKey == NULL) {
        VLOG_ERROR("chef-client", "pubkey_login: publicKey or privateKey is NULL\n");
        errno = EINVAL;
        return -1;
    }

    status = __load_oauth_settings();

    status = __pubkey_sign(privateKey, &signature, &siglen);
    if (status) {
        VLOG_ERROR("chef-client", "pubkey_login: failed to sign message with private key\n");
        return status;
    }

    status = __pubkey_post_login(publicKey, signature, &g_context);
    free(signature);
    if (status) {
        VLOG_ERROR("chef-client", "pubkey_login: failed to post login request\n");
        return status;
    }

    snprintf(&g_bearer[0], sizeof(g_bearer), "Authorization: Bearer %s", g_context.jwt_token);
    return 0;
}

void pubkey_logout(void)
{
    memset(&g_context, 0, sizeof(struct __pubkey_context));
    __save_pubkey_settings();
}

void pubkey_set_authentication(void** headerlist)
{
    struct curl_slist* headers = *headerlist;

    headers = curl_slist_append(headers, &g_bearer[0]);

    *headerlist = headers;
}
