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
#include "../base64/base64.h"
#include <chef/platform.h>
#include <curl/curl.h>
#include <errno.h>
#include <jansson.h>
#include <openssl/pem.h>
#include <openssl/decoder.h>
#include <openssl/err.h>
#include <vlog.h>

extern const char* chefclient_api_base_url(void);
extern json_t*     chefclient_settings(void);

struct __pubkey_context {
    char* account_guid;
    char* jwt_token;
};

// This is the message we will sign using the private key which
// is used to authenticate the user.
static const char*             g_message = "chef-is-an-awesome-tool";
static struct __pubkey_context g_context = { NULL, NULL };
static char                    g_bearer[1024];

static void __load_pubkey_settings(void)
{
    json_t* section;
    json_t* accountGuid;
    json_t* jwtToken;

    if (chefclient_settings() == NULL) {
        VLOG_ERROR("chef-client", "__load_pubkey_settings: chefclient_settings() returned NULL\n");
        return;
    }

    section = json_object_get(chefclient_settings(), "pubkey");
    if (section == NULL) {
        return;
    }

    accountGuid = json_object_get(section, "account-guid");
    if (json_string_value(accountGuid) != NULL) {
        g_context.account_guid = platform_strdup(json_string_value(accountGuid));
    }

    jwtToken = json_object_get(section, "jwt-token");
    if (json_string_value(jwtToken) != NULL) {
        g_context.jwt_token = platform_strdup(json_string_value(jwtToken));
    }
}

static void __save_pubkey_settings(void)
{
    json_t* section;

    if (chefclient_settings() == NULL) {
        return;
    }

    section = json_object();
    if (section == NULL) {
        return;
    }

    if (g_context.account_guid != NULL) {
        json_object_set_new(section, "account-guid", json_string(g_context.account_guid));
    }

    if (g_context.jwt_token != NULL) {
        json_object_set_new(section, "jwt-token", json_string(g_context.jwt_token));
    }
    json_object_set_new(chefclient_settings(), "pubkey", section);
}

static int __pubkey_sign_with_key(EVP_PKEY* pkey, char** signatureOut, size_t* siglenOut)
{
    unsigned char* sig;
    EVP_MD_CTX*    mdctx;
    size_t         siglen = 0;

    mdctx = EVP_MD_CTX_new();
    if (mdctx == NULL) {
        VLOG_ERROR("chef-client", "pubkey_login: failed to create EVP_MD_CTX: %s\n", ERR_error_string(ERR_get_error(), NULL));
        return -1;
    }

    if (EVP_DigestSignInit(mdctx, NULL, EVP_sha256(), NULL, pkey) <= 0) {
        VLOG_ERROR("chef-client", "pubkey_login: failed to initialize digest sign: %s\n", ERR_error_string(ERR_get_error(), NULL));
        EVP_MD_CTX_free(mdctx);
        return -1;
    }

    if (EVP_DigestSignUpdate(mdctx, g_message, strlen(g_message)) <= 0) {
        VLOG_ERROR("chef-client", "pubkey_login: failed to update digest sign: %s\n", ERR_error_string(ERR_get_error(), NULL));
        EVP_MD_CTX_free(mdctx);
        return -1;
    }

    if (EVP_DigestSignFinal(mdctx, NULL, &siglen) <= 0) {
        VLOG_ERROR("chef-client", "pubkey_login: failed to finalize digest sign: %s\n", ERR_error_string(ERR_get_error(), NULL));
        EVP_MD_CTX_free(mdctx);
        return -1;
    }

    sig = malloc(siglen);
    if (sig == NULL) {
        VLOG_ERROR("chef-client", "pubkey_login: failed to allocate memory for signature\n");
        EVP_MD_CTX_free(mdctx);
        return -1;
    }

    if (EVP_DigestSignFinal(mdctx, sig, &siglen) <= 0) {
        VLOG_ERROR("chef-client", "pubkey_login: failed to finalize digest sign with signature: %s\n", ERR_error_string(ERR_get_error(), NULL));
        free(sig);
        EVP_MD_CTX_free(mdctx);
        return -1;
    }
    EVP_MD_CTX_free(mdctx);

    *signatureOut = (char*)sig;
    *siglenOut = siglen;
    return 0;
}

static int __pubkey_sign(const char* privateKey, const char* password, char** signatureOut, size_t* siglenOut)
{
    BIO*              keybio;
    int               status;
    OSSL_DECODER_CTX* dctx;
    EVP_PKEY*         pkey = NULL;
    
    const char* structure = NULL; /* any structure */
    const char* format = NULL;   /* any format (DER, etc) */
    const char* keytype = NULL;   /* NULL for any key (RSA, EC etc) */

    VLOG_DEBUG("chef-client", "__pubkey_sign(privateKey=%s)\n", privateKey);

    keybio = BIO_new_file(privateKey, "rb");
    if (keybio == NULL) {
        VLOG_ERROR("chef-client", "pubkey_login: failed to open private key file %s: %s\n", privateKey, strerror(errno));
        return -1;
    }

    dctx = OSSL_DECODER_CTX_new_for_pkey(
        &pkey, format, structure, keytype,
        OSSL_KEYMGMT_SELECT_PRIVATE_KEY | OSSL_KEYMGMT_SELECT_DOMAIN_PARAMETERS,
        NULL, NULL
    );
    if (dctx == NULL) {
        VLOG_ERROR("chef-client", "pubkey_login: failed to create decoder context: %s\n", ERR_error_string(ERR_get_error(), NULL));
        BIO_free_all(keybio);
        return -1;
    }

    if (password != NULL) {
        OSSL_DECODER_CTX_set_passphrase(dctx, (const unsigned char*)password, strlen(password));
    }

    // Returns 0 on failure, 1 on success
    if (OSSL_DECODER_from_bio(dctx, keybio)) {
        status = __pubkey_sign_with_key(pkey, signatureOut, siglenOut);
    } else { 
        VLOG_ERROR("chef-client", "pubkey_login: failed to decode private key: %s\n", ERR_error_string(ERR_get_error(), NULL));
        status = -1;
    }
    OSSL_DECODER_CTX_free(dctx);
    BIO_free_all(keybio);
    return status;
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

static int __read_public_key(const char* publicKeyPath, char** publicKeyOut)
{
    void*  keyBuffer;
    char*  tmp;
    size_t keylen;
    int    status;
    size_t j = 0;

    status = platform_readfile(publicKeyPath, &keyBuffer, &keylen);
    if (status) {
        VLOG_ERROR("chef-client", "__read_public_key: failed to read public key file %s: %s\n", publicKeyPath, strerror(errno));
        return -1;
    }

    // replace newlines with \n and remove \r
    tmp = (char*)malloc((keylen * 2) + 1);
    if (tmp == NULL) {
        free(keyBuffer);
        return -1;
    }

    for (size_t i = 0; i < keylen; i++) {
        if (((char*)keyBuffer)[i] == '\n') {
            tmp[j++] = '\\';
            tmp[j++] = 'n';
        } else if (((char*)keyBuffer)[i] != '\r') {
            tmp[j++] = ((char*)keyBuffer)[i];
        }
    }
    tmp[j] = '\0';

    *publicKeyOut = platform_strdup(tmp);

    free(tmp);
    free(keyBuffer);
    return 0;
}

static int __pubkey_post_login(const char* email, const char* publicKey, const char* signature, struct __pubkey_context* context)
{
    char                 buffer[4096];
    char                 url[1024];
    char*                keyBuffer;
    struct chef_request* request;
    CURLcode             code;
    long                 httpCode;
    int                  status = -1;
    VLOG_DEBUG("chef-client", "__pubkey_post_login(publicKey=%s)\n", publicKey);

    if (__read_public_key(publicKey, &keyBuffer)) {
        VLOG_ERROR("chef-client", "__pubkey_post_login: failed to read public key file %s: %s\n", publicKey, strerror(errno));
        return -1;
    }

    request = chef_request_new(CHEF_CLIENT_API_SECURE, 0);
    if (!request) {
        VLOG_ERROR("chef-client", "__pubkey_post_login: failed to create request\n");
        free(keyBuffer);
        return -1;
    }

    // Set the content type and accept headers
    request->headers = curl_slist_append(request->headers, "Content-Type: application/json");
    request->headers = curl_slist_append(request->headers, "Accept: application/json");

    // Set the URL and headers
    snprintf(&url[0], sizeof(url), "%s/account/login", chefclient_api_base_url());
    code = curl_easy_setopt(request->curl, CURLOPT_URL, &url[0]);
    if (code != CURLE_OK) {
        VLOG_ERROR("chef-client", "__pubkey_post_login: failed to set url [%s]\n", request->error);
        goto cleanup;
    }

    // format the json body
    snprintf(
        buffer,
        sizeof(buffer),
        "{\"Email\":\"%s\",\"PublicKey\":\"%s\",\"SecurityToken\":\"%s\"}",
        email, keyBuffer, signature
    );

    code = curl_easy_setopt(request->curl, CURLOPT_POSTFIELDS, &buffer[0]);
    if (code != CURLE_OK) {
        VLOG_ERROR("chef-client", "__pubkey_post_login: failed to set body [%s]\n", request->error);
        goto cleanup;
    }

    code = chef_request_execute(request);
    if (code != CURLE_OK) {
        goto cleanup;
    }

    curl_easy_getinfo(request->curl, CURLINFO_RESPONSE_CODE, &httpCode);
    if (httpCode != 200) {
        VLOG_ERROR("chef-client", "__pubkey_post_login: http error %ld [%s]\n", httpCode, request->response);
        errno = EIO;
        goto cleanup;
    }

    status = __parse_token_response(request->response, context);

cleanup:
    chef_request_delete(request);
    free(keyBuffer);
    return status;
}

int pubkey_login(const char* email, const char* publicKey, const char* privateKey)
{
    char*  signature = NULL;
    size_t siglen = 0;
    int    status;
    VLOG_DEBUG("chef-client", "pubkey_login(publicKey=%s, privateKey=%s)\n", publicKey, privateKey);

    // Use openSSL libraries to sign the message with the private key, and then
    // we use the signed message and our public key in the chef api headers to 
    // prove who we are.
    if (publicKey == NULL || privateKey == NULL) {
        VLOG_ERROR("chef-client", "pubkey_login: publicKey or privateKey is NULL\n");
        errno = EINVAL;
        return -1;
    }

    // attempt to load any existing settings
    __load_pubkey_settings();

    if (g_context.jwt_token == NULL) {
        char*  base64Signature = NULL;
        size_t base64SignatureLength = 0;

        status = __pubkey_sign(privateKey, NULL, &signature, &siglen);
        if (status) {
            VLOG_ERROR("chef-client", "pubkey_login: failed to sign message with private key\n");
            return status;
        }

        base64Signature = base64_encode((unsigned char*)signature, siglen, 0, &base64SignatureLength);
        free(signature);
        if (base64Signature == NULL) {
            VLOG_ERROR("chef-client", "pubkey_login: failed to base64 encode signature\n");
            return -1;
        }

        status = __pubkey_post_login(email, publicKey, base64Signature, &g_context);
        free(base64Signature);
        if (status) {
            VLOG_ERROR("chef-client", "pubkey_login: failed to post login request\n");
            return status;
        }
        __save_pubkey_settings();
    }

    snprintf(&g_bearer[0], sizeof(g_bearer), "Authorization: Bearer %s", g_context.jwt_token);
    return 0;
}

int pubkey_generate_rsa_keypair(int bits, const char* directory, char** publicKeyPath, char** privateKeyPath)
{
    // use openssl library to generate a new rsa keypair
    // save the keys to files in the .chef directory
    // return the paths to the files
    EVP_PKEY*      pkey;
    FILE*          pubfp = NULL;
    FILE*          privfp = NULL;
    char           pubkeypath[PATH_MAX];
    char           privkeypath[PATH_MAX];
    int            status;
    VLOG_DEBUG("chef-client", "pubkey_generate_rsa_keypair(bits=%d)\n", bits);

    snprintf(&pubkeypath[0], sizeof(pubkeypath), "%s/id.pub", directory);
    snprintf(&privkeypath[0], sizeof(privkeypath), "%s/id_rsa", directory);

    VLOG_DEBUG("chef-client", "pubkey_generate_rsa_keypair: generating new rsa keypair %s/%s\n",
        &pubkeypath[0], &privkeypath[0]);
    pkey = EVP_RSA_gen(bits);
    if (pkey == NULL) {
        VLOG_ERROR("chef-client", "pubkey_generate_rsa_keypair: failed to generate RSA key: %s\n",
            ERR_error_string(ERR_get_error(), NULL));
        return -1;
    }

    privfp = fopen(&privkeypath[0], "wb");
    if (privfp == NULL) {
        VLOG_ERROR("chef-client", "pubkey_generate_rsa_keypair: failed to open private key file %s: %s\n",
            &privkeypath[0], strerror(errno));
        status = -1;
        goto cleanup;
    }

    status = PEM_write_PrivateKey(privfp, pkey, NULL, NULL, 0, NULL, NULL);
    if (status <= 0) {
        VLOG_ERROR("chef-client", "pubkey_generate_rsa_keypair: failed to write private key to file %s: %s\n",
            &privkeypath[0], ERR_error_string(ERR_get_error(), NULL));
        status = -1;
        goto cleanup;
    }

    pubfp = fopen(&pubkeypath[0], "wb");
    if (pubfp == NULL) {
        VLOG_ERROR("chef-client", "pubkey_generate_rsa_keypair: failed to open public key file %s: %s\n",
            &pubkeypath[0], strerror(errno));
        goto cleanup;
    }

    status = PEM_write_PUBKEY(pubfp, pkey);
    if (status <= 0) {
        VLOG_ERROR("chef-client", "pubkey_generate_rsa_keypair: failed to write public key to file %s: %s\n",
            &pubkeypath[0], ERR_error_string(ERR_get_error(), NULL));
        status = -1;
        goto cleanup;
    }

    *publicKeyPath = platform_strdup(&pubkeypath[0]);
    *privateKeyPath = platform_strdup(&privkeypath[0]);

    // we use 0 as success
    status = 0;

cleanup:
    if (privfp != NULL) {
        fclose(privfp);
    }
    if (pubfp != NULL) {
        fclose(pubfp);
    }
    EVP_PKEY_free(pkey);
    return status;
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
