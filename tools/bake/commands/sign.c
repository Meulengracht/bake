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

#include <chef/config.h>
#include <chef/dirs.h>
#include <chef/package_manifest.h>
#include <chef/platform.h>
#include <errno.h>
#include <openssl/decoder.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vlog.h>

#include "commands.h"

struct __dev_proof_options {
    char* developer_identity;
    char* public_key_path;
    char* private_key_path;
};

struct __developer_proof {
    char* hash;
    char* public_key;
    char* signature;
};

static int __copy_config_value(
    struct chef_config* config,
    void*               accountSection,
    const char*         key,
    char**              valueOut)
{
    const char* value = chef_config_get_string(config, accountSection, key);
    if (value == NULL || value[0] == '\0') {
        return -1;
    }

    *valueOut = platform_strdup(value);
    return *valueOut == NULL ? -1 : 0;
}

static void __cleanup_dev_proof_options(struct __dev_proof_options* options)
{
    if (options == NULL) {
        return;
    }

    free(options->developer_identity);
    free(options->public_key_path);
    free(options->private_key_path);
    memset(options, 0, sizeof(struct __dev_proof_options));
}

static int __load_dev_proof_options(struct __dev_proof_options* options)
{
    struct chef_config* config;
    void*               accountSection;

    memset(options, 0, sizeof(struct __dev_proof_options));

    config = chef_config_load(chef_dirs_config());
    if (config == NULL) {
        VLOG_ERROR("bake", "failed to load configuration from %s\n", chef_dirs_config());
        return -1;
    }

    accountSection = chef_config_section(config, "account");
    if (accountSection == NULL) {
        VLOG_ERROR("bake", "missing account configuration section\n");
        return -1;
    }

    if (__copy_config_value(config, accountSection, "email", &options->developer_identity) != 0 ||
        __copy_config_value(config, accountSection, "public-key", &options->public_key_path) != 0 ||
        __copy_config_value(config, accountSection, "private-key", &options->private_key_path) != 0) {
        printf("developer identity configuration is incomplete, please run the following command\n");
        printf("  order account whoami\n");
        __cleanup_dev_proof_options(options);
        errno = EINVAL;
        return -1;
    }

    VLOG_DEBUG("bake", "loaded developer proof options: identity=%s, public_key_path=%s, private_key_path=%s\n",
               options->developer_identity, options->public_key_path, options->private_key_path);
    return 0;
}

static int __encode_base64(const unsigned char* data, size_t dataLength, char** encodedOut)
{
    int   encodedLength;
    char* encoded;

    encodedLength = 4 * ((int)(dataLength + 2) / 3);
    encoded = calloc((size_t)encodedLength + 1, 1);
    if (encoded == NULL) {
        return -1;
    }

    if (EVP_EncodeBlock((unsigned char*)encoded, data, (int)dataLength) < 0) {
        free(encoded);
        return -1;
    }

    *encodedOut = encoded;
    return 0;
}

static int __calculate_file_sha512(const char* path, unsigned char hash[EVP_MAX_MD_SIZE], unsigned int* hashLength)
{
    FILE*         file;
    EVP_MD_CTX*   mdctx;
    unsigned char buffer[4096];
    size_t        read;

    file = fopen(path, "rb");
    if (file == NULL) {
        return -1;
    }

    mdctx = EVP_MD_CTX_new();
    if (mdctx == NULL) {
        fclose(file);
        return -1;
    }

    if (EVP_DigestInit_ex(mdctx, EVP_sha512(), NULL) != 1) {
        EVP_MD_CTX_free(mdctx);
        fclose(file);
        return -1;
    }

    while ((read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        if (EVP_DigestUpdate(mdctx, buffer, read) != 1) {
            EVP_MD_CTX_free(mdctx);
            fclose(file);
            return -1;
        }
    }

    if (ferror(file) || EVP_DigestFinal_ex(mdctx, hash, hashLength) != 1) {
        EVP_MD_CTX_free(mdctx);
        fclose(file);
        return -1;
    }

    EVP_MD_CTX_free(mdctx);
    fclose(file);
    return 0;
}

static const char* __openssl_error_string(char* buffer, size_t len)
{
    unsigned long errorCode = ERR_get_error();

    if (errorCode == 0) {
        return "no OpenSSL error details";
    }

    ERR_error_string_n(errorCode, buffer, len - 1);
    return buffer;
}

static int __private_key_uses_openssh_format(const char* privateKeyPath)
{
    char  header[64] = { 0 };
    FILE* file;

    file = fopen(privateKeyPath, "rb");
    if (file == NULL) {
        return 0;
    }

    if (fgets(header, sizeof(header), file) == NULL) {
        fclose(file);
        return 0;
    }

    fclose(file);
    return strcmp(header, "-----BEGIN OPENSSH PRIVATE KEY-----\n") == 0 ? 1 : 0;
}

static int __load_private_key(BIO* keybio, EVP_PKEY** pkeyOut, char* errorBuffer, size_t errorBufferLength)
{
    OSSL_DECODER_CTX* dctx;
    EVP_PKEY*         pkey = NULL;

    dctx = OSSL_DECODER_CTX_new_for_pkey(
        &pkey,
        NULL,
        NULL,
        NULL,
        OSSL_KEYMGMT_SELECT_PRIVATE_KEY | OSSL_KEYMGMT_SELECT_DOMAIN_PARAMETERS,
        NULL,
        NULL
    );
    if (dctx == NULL) {
        VLOG_ERROR("bake", "failed to create private key decoder: %s\n", __openssl_error_string(errorBuffer, errorBufferLength));
        return -1;
    }

    if (OSSL_DECODER_from_bio(dctx, keybio) != 1) {
        OSSL_DECODER_CTX_free(dctx);
        return -1;
    }

    OSSL_DECODER_CTX_free(dctx);
    *pkeyOut = pkey;
    return 0;
}

static int __sign_hash(const char* privateKeyPath, const unsigned char* hash, size_t hashLength, char** signatureOut)
{
    BIO*           keybio;
    EVP_PKEY*      pkey = NULL;
    EVP_MD_CTX*    mdctx = NULL;
    unsigned char* signature = NULL;
    size_t         signatureLength = 0;
    char*          encoded = NULL;
    char           errorBuffer[256] = { 0 };
    int            status = -1;
    VLOG_DEBUG("bake", "__sign_hash(privateKey=%s)\n", privateKeyPath);

    keybio = BIO_new_file(privateKeyPath, "rb");
    if (keybio == NULL) {
        VLOG_ERROR(
            "bake",
            "failed to open private key %s\n", 
            privateKeyPath
        );
        return -1;
    }

    if (__load_private_key(keybio, &pkey, errorBuffer, sizeof(errorBuffer)) != 0) {
        if (__private_key_uses_openssh_format(privateKeyPath)) {
            VLOG_ERROR(
                "bake",
                "private key %s uses OpenSSH private-key format; bake sign expects a PEM-encoded private key\n",
                privateKeyPath
            );
        }
        VLOG_ERROR(
            "bake",
            "failed to read private key from %s: %s\n",
            privateKeyPath,
            __openssl_error_string(errorBuffer, sizeof(errorBuffer))
        );
        BIO_free_all(keybio);
        return -1;
    }

    BIO_free_all(keybio);

    mdctx = EVP_MD_CTX_new();
    if (mdctx == NULL) {
        VLOG_ERROR("bake", "failed to create message digest context: %s\n", __openssl_error_string(errorBuffer, sizeof(errorBuffer)));
        goto cleanup;
    }

    if (EVP_DigestSignInit(mdctx, NULL, EVP_sha512(), NULL, pkey) <= 0) {
        VLOG_ERROR("bake", "failed to initialize digest sign context: %s\n", __openssl_error_string(errorBuffer, sizeof(errorBuffer)));
        goto cleanup;
    }

    if (EVP_DigestSignUpdate(mdctx, hash, hashLength) <= 0) {
        VLOG_ERROR("bake", "failed to update digest sign context: %s\n", __openssl_error_string(errorBuffer, sizeof(errorBuffer)));
        goto cleanup;
    }

    if (EVP_DigestSignFinal(mdctx, NULL, &signatureLength) <= 0) {
        VLOG_ERROR("bake", "failed to finalize digest sign context: %s\n", __openssl_error_string(errorBuffer, sizeof(errorBuffer)));
        goto cleanup;
    }

    signature = malloc(signatureLength);
    if (signature == NULL) {
        VLOG_ERROR("bake", "failed to allocate memory for signature\n");
        goto cleanup;
    }

    if (EVP_DigestSignFinal(mdctx, signature, &signatureLength) <= 0) {
        VLOG_ERROR("bake", "failed to finalize digest sign context: %s\n", __openssl_error_string(errorBuffer, sizeof(errorBuffer)));
        goto cleanup;
    }

    if (__encode_base64(signature, signatureLength, &encoded)) {
        VLOG_ERROR("bake", "failed to encode signature\n");
        goto cleanup;
    }

    *signatureOut = encoded;
    encoded = NULL;
    status = 0;

cleanup:
    EVP_MD_CTX_free(mdctx);
    EVP_PKEY_free(pkey);
    free(signature);
    free(encoded);
    return status;
}

static void __developer_proof_cleanup(struct __developer_proof* proof)
{
    if (proof == NULL) {
        return;
    }

    free(proof->hash);
    free(proof->public_key);
    free(proof->signature);
}

static int __create_developer_proof(const char* packPath, const struct __dev_proof_options* options, struct __developer_proof* proof)
{
    void*          publicKeyBuffer;
    size_t         publicKeyLength;
    unsigned char  hash[EVP_MAX_MD_SIZE];
    unsigned int   hashLength;
    int            status;
    VLOG_DEBUG("bake", "__create_developer_proof(packPath=%s)\n", packPath);

    memset(proof, 0, sizeof(struct __developer_proof));

    status = platform_readfile(options->public_key_path, &publicKeyBuffer, &publicKeyLength);
    if (status) {
        VLOG_ERROR("bake", "failed to read public key from %s\n", options->public_key_path);
        return status;
    }

    status = __calculate_file_sha512(packPath, hash, &hashLength);
    if (status) {
        VLOG_ERROR("bake", "failed to calculate hash for %s\n", packPath);
        goto cleanup;
    }

    status = __encode_base64(hash, hashLength, &proof->hash);
    if (status) {
        VLOG_ERROR("bake", "failed to encode hash for %s\n", packPath);
        goto cleanup;
    }

    status = __encode_base64((const unsigned char*)publicKeyBuffer, publicKeyLength, &proof->public_key);
    if (status) {
        VLOG_ERROR("bake", "failed to encode public key for %s\n", packPath);
        goto cleanup;
    }
    
    status = __sign_hash(options->private_key_path, hash, hashLength, &proof->signature);
    if (status) {
        VLOG_ERROR("bake", "failed to sign hash for %s\n", packPath);
    }

cleanup:
    free(publicKeyBuffer);
    return status;
}

static int __write_developer_proof_file(
    const char*                        packPath,
    const char*                        packageName,
    const struct __dev_proof_options*  options,
    struct __developer_proof*          proof)
{
    char* proofPath;
    FILE* file;

    // <pack-path>.proof
    proofPath = malloc(strlen(packPath) + 6 + 1);
    if (proofPath == NULL) {
        VLOG_ERROR("bake", "failed to allocate memory for proof path\n");
        return -1;
    }

    sprintf(proofPath, "%s.proof", packPath);
    file = fopen(proofPath, "wb");
    if (file == NULL) {
        VLOG_ERROR("bake", "failed to open proof file %s for writing\n", proofPath);
        free(proofPath);
        return -1;
    }

    fprintf(
        file,
        "{\n"
        "  \"origin\": \"developer\",\n"
        "  \"identity\": \"%s\",\n"
        "  \"package\": \"%s\",\n"
        "  \"hash-algorithm\": \"sha512\",\n"
        "  \"hash\": \"%s\",\n"
        "  \"public-key\": \"%s\",\n"
        "  \"signature\": \"%s\"\n"
        "}\n",
        options->developer_identity,
        packageName,
        proof->hash,
        proof->public_key,
        proof->signature
    );

    fclose(file);
    free(proofPath);
    return 0;
}

int bake_write_dev_proof(const char* packPath)
{
    struct __dev_proof_options options = { 0 };
    struct __developer_proof   proof = { 0 };
    struct chef_package_manifest* manifest = NULL;
    struct platform_stat       stats;
    int                        status;

    if (packPath == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (platform_stat(packPath, &stats) != 0 || stats.type != PLATFORM_FILETYPE_FILE) {
        errno = ENOENT;
        return -1;
    }

    status = __load_dev_proof_options(&options);
    if (status) {
        VLOG_ERROR("bake", "failed to load proof configuration\n");
        return status;
    }

    status = chef_package_manifest_load(packPath, &manifest);
    if (status) {
        VLOG_ERROR("bake", "failed to load package %s\n", packPath);
        goto cleanup;
    }

    status = __create_developer_proof(packPath, &options, &proof);
    if (status) {
        VLOG_ERROR("bake", "failed to generate proof for %s\n", packPath);
        goto cleanup;
    }

    status = __write_developer_proof_file(packPath, manifest->name, &options, &proof);
    if (status) {
        VLOG_ERROR("bake", "unable to write developer proof for %s\n", packPath);
    }

cleanup:
    chef_package_manifest_free(manifest);
    __developer_proof_cleanup(&proof);
    __cleanup_dev_proof_options(&options);
    return status;
}

static void __print_help(void)
{
    printf("Usage: bake sign <package-file>\n");
    printf("\n");
    printf("Options:\n");
    printf("  -h, --help\n");
    printf("      Shows this help message\n");
}

int sign_main(int argc, char** argv, char** envp, struct bake_command_options* options)
{
    const char* packPath = NULL;
    int         status;

    (void)envp;
    (void)options;

    for (int i = 1; i < argc; i++) {
        if (__cli_is_help_switch(argv[i])) {
            __print_help();
            return 0;
        }

        if (argv[i][0] != '-') {
            if (packPath != NULL && strcmp(packPath, argv[i]) != 0) {
                fprintf(stderr, "bake: only one package file can be specified\n");
                return -1;
            }
            packPath = argv[i];
            continue;
        }

        fprintf(stderr, "bake: unknown option %s\n", argv[i]);
        __print_help();
        return -1;
    }

    if (packPath == NULL) {
        fprintf(stderr, "bake: no package file specified\n");
        __print_help();
        return -1;
    }

    status = bake_write_dev_proof(packPath);
    if (status != 0) {
        fprintf(stderr, "bake: failed to sign package %s: %s\n", packPath, strerror(errno));
        return status;
    }

    printf("wrote developer proof: %s.proof\n", packPath);
    return 0;
}
