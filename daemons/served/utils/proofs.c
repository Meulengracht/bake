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

#include <chef/store.h>
#include <chef/package.h>
#include <chef/platform.h>
#include <jansson.h>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <errno.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <string.h>
#include <vlog.h>

#define _SEGMENT_SIZE (1024 * 1024)

static int __calculate_file_sha512(const char* path, unsigned char** hash, unsigned int* hashLength);

static void __print_crypt_errors(const char* prefix)
{
    unsigned long status;
    char          tmp[256];

    while ((status = ERR_get_error()) != 0) {
        ERR_error_string_n(status, tmp, sizeof(tmp));
        VLOG_ERROR("served", "%s: %s\n", prefix, &tmp[0]);
    }
}

static int __verify_signature(EVP_PKEY* key, const char* data, size_t dataLength, const unsigned char* signature, size_t signaturelength) {
    EVP_MD_CTX* mdctx;
    int         status, r;

    mdctx = EVP_MD_CTX_create();
    if (mdctx == NULL) {
        VLOG_ERROR("served", "__verify_signature: failed to create digest context\n");
        return -1;
    }

    status = EVP_DigestVerifyInit(mdctx, NULL, EVP_sha512(), NULL, key);
    if (status != 1) {
        VLOG_ERROR("served", "__verify_signature: failed to initialize digest context\n");
        __print_crypt_errors("__verify_signature: digest init");
        EVP_MD_CTX_free(mdctx);
        return -1;
    }

    status = EVP_DigestVerifyUpdate(mdctx, data, dataLength);
    if (status != 1) {
        VLOG_ERROR("served", "__verify_signature: failed to process digest data\n");
        __print_crypt_errors("__verify_signature: digest update");
        EVP_MD_CTX_free(mdctx);
        return -1;
    }

    r = EVP_DigestVerifyFinal(mdctx, signature, signaturelength);
    if (r == 1) {
        status = 0;
    } else {
        if (r == 0) {
            VLOG_ERROR("served", "__verify_signature: proof signature did not match\n");
        } else {
            VLOG_ERROR("served", "__verify_signature: failed to verify proof signature\n");
            __print_crypt_errors("__verify_signature: digest final");
        }
        status = -1;
    }
    EVP_MD_CTX_free(mdctx);
    return status;
}

static int __base64_decode(const char* value, unsigned char** dataOut, size_t* dataLengthOut)
{
    size_t         valueLength;
    unsigned char* buffer;
    int            decodedLength;

    if (value == NULL) {
        VLOG_ERROR("served", "__base64_decode: cannot decode NULL value\n");
        errno = EINVAL;
        return -1;
    }

    valueLength = strlen(value);
    buffer = calloc((valueLength * 3) / 4 + 4, 1);
    if (buffer == NULL) {
        VLOG_ERROR("served", "__base64_decode: failed to allocate decode buffer for %zu-byte input\n", valueLength);
        return -1;
    }

    decodedLength = EVP_DecodeBlock(buffer, (const unsigned char*)value, (int)valueLength);
    if (decodedLength < 0) {
        VLOG_ERROR("served", "__base64_decode: failed to decode base64 payload\n");
        free(buffer);
        errno = EINVAL;
        return -1;
    }

    while (valueLength > 0 && value[valueLength - 1] == '=') {
        decodedLength--;
        valueLength--;
    }

    *dataOut = buffer;
    *dataLengthOut = (size_t)decodedLength;
    return 0;
}

static int __parse_proof_origin(const char* origin, enum chef_package_proof_origin* originOut)
{
    if (origin == NULL || originOut == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (strcmp(origin, "developer") == 0) {
        *originOut = CHEF_PACKAGE_PROOF_ORIGIN_DEVELOPER;
        return 0;
    }

    if (strcmp(origin, "publisher") == 0 || strcmp(origin, "store") == 0) {
        *originOut = CHEF_PACKAGE_PROOF_ORIGIN_STORE;
        return 0;
    }

    VLOG_ERROR("served", "__parse_proof_origin: unsupported proof origin '%s'\n", origin);
    errno = ENOTSUP;
    return -1;
}

static int __parse_public_key_pem(const unsigned char* key, size_t keyLength, EVP_PKEY** keyOut)
{
    BIO* bio;

    bio = BIO_new_mem_buf(key, (int)keyLength);
    if (bio == NULL) {
        VLOG_ERROR("served", "__parse_public_key_pem: failed to create BIO for %zu-byte public key\n", keyLength);
        return -1;
    }

    *keyOut = PEM_read_bio_PUBKEY(bio, NULL, NULL, NULL);
    BIO_free_all(bio);
    if (*keyOut == NULL) {
        VLOG_ERROR("served", "__parse_public_key_pem: failed to parse PEM public key\n");
        __print_crypt_errors("__parse_public_key_pem");
        errno = EINVAL;
    }
    return *keyOut == NULL ? -1 : 0;
}

static int __verify_package_proof(
    struct chef_package_proof*     proof,
    const char*                    packagePath,
    enum chef_package_proof_origin expectedOrigin)
{
    unsigned char* expectedHash = NULL;
    size_t         expectedHashLength = 0;
    unsigned char* publicKeyPem = NULL;
    size_t         publicKeyPemLength = 0;
    unsigned char* signature = NULL;
    size_t         signatureLength = 0;
    unsigned char* actualHash = NULL;
    unsigned int   actualHashLength = 0;
    EVP_PKEY*      publicKey = NULL;
    int            status = -1;

    if (proof == NULL || proof->origin == CHEF_PACKAGE_PROOF_ORIGIN_NONE) {
        VLOG_ERROR("served", "__verify_package_proof: invalid package proof\n");
        errno = EINVAL;
        return -1;
    }

    if (expectedOrigin != CHEF_PACKAGE_PROOF_ORIGIN_NONE && proof->origin != expectedOrigin) {
        VLOG_ERROR("served", "__verify_package_proof: proof origin %d did not match expected %d\n", proof->origin, expectedOrigin);
        errno = EINVAL;
        return -1;
    }

    if (proof->hash_algorithm == NULL || strcmp(proof->hash_algorithm, "sha512") != 0) {
        VLOG_ERROR(
            "served",
            "__verify_package_proof: unsupported hash algorithm %s\n",
            proof->hash_algorithm != NULL ? proof->hash_algorithm : "<null>"
        );
        errno = ENOTSUP;
        return -1;
    }

    status = __base64_decode(proof->hash, &expectedHash, &expectedHashLength);
    if (status != 0) {
        VLOG_ERROR("served", "__verify_package_proof: failed to decode proof hash for %s\n", packagePath);
        goto cleanup;
    }

    status = __base64_decode(proof->signature, &signature, &signatureLength);
    if (status != 0) {
        VLOG_ERROR("served", "__verify_package_proof: failed to decode proof signature for %s\n", packagePath);
        goto cleanup;
    }

    status = __calculate_file_sha512(packagePath, &actualHash, &actualHashLength);
    if (status != 0) {
        VLOG_ERROR("served", "__verify_package_proof: failed to calculate package hash for %s\n", packagePath);
        goto cleanup;
    }

    if (expectedHashLength != actualHashLength || memcmp(expectedHash, actualHash, actualHashLength) != 0) {
        VLOG_ERROR("served", "__verify_package_proof: hash mismatch for %s\n", packagePath);
        errno = EBADMSG;
        goto cleanup;
    }

    status = __base64_decode(proof->public_key, &publicKeyPem, &publicKeyPemLength);
    if (status != 0) {
        VLOG_ERROR("served", "__verify_package_proof: failed to decode public key for %s\n", packagePath);
        goto cleanup;
    }

    status = __parse_public_key_pem(publicKeyPem, publicKeyPemLength, &publicKey);
    if (status != 0) {
        VLOG_ERROR("served", "__verify_package_proof: failed to parse proof public key for %s\n", packagePath);
        goto cleanup;
    }

    status = __verify_signature(publicKey, (const char*)actualHash, actualHashLength, signature, signatureLength);
    if (status != 0) {
        VLOG_ERROR("served", "__verify_package_proof: signature verification failed for %s\n", packagePath);
    }

cleanup:
    OPENSSL_free(actualHash);
    EVP_PKEY_free(publicKey);
    free(expectedHash);
    free(publicKeyPem);
    free(signature);
    return status;
}

static int __load_proof(const char* proofPath, struct chef_package_proof** proofOut)
{
    json_t*                    root;
    json_error_t               error;
    struct chef_package_proof* proof;

    root = json_load_file(proofPath, 0, &error);
    if (root == NULL) {
        return -1;
    }

    proof = calloc(1, sizeof(struct chef_package_proof));
    if (proof == NULL) {
        json_decref(root);
        return -1;
    }

    if (__parse_proof_origin(json_string_value(json_object_get(root, "origin")), &proof->origin) != 0) {
        json_decref(root);
        free(proof);
        return -1;
    }

    proof->identity = platform_strdup(json_string_value(json_object_get(root, "identity")));
    proof->hash_algorithm = platform_strdup(json_string_value(json_object_get(root, "hash-algorithm")));
    proof->hash = platform_strdup(json_string_value(json_object_get(root, "hash")));
    proof->public_key = platform_strdup(json_string_value(json_object_get(root, "public-key")));
    proof->signature = platform_strdup(json_string_value(json_object_get(root, "signature")));
    json_decref(root);

    if (proof->identity == NULL || proof->hash_algorithm == NULL || proof->hash == NULL ||
        proof->public_key == NULL || proof->signature == NULL) {
        chef_package_proof_free(proof);
        errno = EINVAL;
        return -1;
    }

    *proofOut = proof;
    return 0;
}

int utils_load_local_package_proof(const char* proofPath, struct chef_package_proof** proofOut)
{
    if (proofOut != NULL) {
        *proofOut = NULL;
    }

    if (proofPath == NULL || proofPath[0] == '\0' || proofOut == NULL) {
        errno = EINVAL;
        return -1;
    }

    return __load_proof(proofPath, proofOut);
}

static int __calculate_file_sha512(const char* path, unsigned char** hash, unsigned int* hashLength)
{
    FILE*       file;
    char*       buffer;
    int         status;
    EVP_MD_CTX* mdctx;

    file = fopen(path, "rb");
    if (!file) {
        VLOG_ERROR("served", "__calculate_file_sha512: failed to open package path %s\n", path);
        return -1;
    }

    buffer = (char*)malloc(_SEGMENT_SIZE);
    if (buffer == NULL) {
        VLOG_ERROR("served", "__calculate_file_sha512: failed to allocate read buffer for %s\n", path);
        fclose(file);
        return -1;
    }

    mdctx = EVP_MD_CTX_new();
    if (mdctx == NULL) {
        VLOG_ERROR("served", "__calculate_file_sha512: failed to allocate SHA512 context for %s\n", path);
        status = -1;
        goto cleanup;
    }
    
    status = EVP_DigestInit_ex(mdctx, EVP_sha512(), NULL);
    if (status <= 0) {
        VLOG_ERROR("served", "__calculate_file_sha512: failed to initialize SHA512 state for %s\n", path);
        __print_crypt_errors("__calculate_file_sha512: digest init");
        status = -1;
        goto cleanup;
    }

    for (;;) {
        size_t read;

        read = fread(buffer, 1, _SEGMENT_SIZE, file);
        if (read == 0) {
            break;
        }

        status = EVP_DigestUpdate(mdctx, buffer, read);
        if (status <= 0) {
            VLOG_ERROR("served", "__calculate_file_sha512: failed to update SHA512 digest for %s\n", path);
            __print_crypt_errors("__calculate_file_sha512: digest update");
            status = -1;
            goto cleanup;
        }

        // was it last segment?
        if (read < _SEGMENT_SIZE) {
            break;
        }
    }

    *hash = (unsigned char *)OPENSSL_malloc(EVP_MD_size(EVP_sha512()));
    if(*hash == NULL) {
        VLOG_ERROR("served", "__calculate_file_sha512: failed to allocate checksum buffer for %s\n", path);
        status = -1;
        goto cleanup;
    }

    status = EVP_DigestFinal_ex(mdctx, *hash, hashLength);
    if (status <= 0) {
        VLOG_ERROR("served", "__calculate_file_sha512: failed to finalize SHA512 digest for %s\n", path);
        __print_crypt_errors("__calculate_file_sha512: digest final");
        status = -1;
    }

    // reset to 0 for success
    status = 0;

cleanup:
    free(buffer);
    fclose(file);
    EVP_MD_CTX_free(mdctx);
    return status;
}

static int __verify_store_package(const char* packagePath, const char* publisher, const char* package, int revision)
{
    int                        status;
    char                       key[128];
    struct store_proof_package proof = { 0 };

    proof_format_package_key(&key[0], sizeof(key), publisher, package, revision);

    status = store_proof_ensure(STORE_PROOF_PACKAGE, &key[0], NULL);
    if (status) {
        VLOG_ERROR("served", "__verify_store_package: failed to ensure proof for package %s\n", &key[0]);
        return status;
    }

    status = store_proof_lookup(STORE_PROOF_PACKAGE, &key[0], &proof);
    if (status) {
        VLOG_ERROR("served", "__verify_store_package: failed to load proof for package %s\n", &key[0]);
        return status;
    }

    status = __verify_package_proof(&proof.proof, packagePath, CHEF_PACKAGE_PROOF_ORIGIN_STORE);
    if (status) {
        VLOG_ERROR("served", "__verify_store_package: failed to verify proof for %s/%s revision %d\n", publisher, package, revision);
    }
    return status;
}

int utils_verify_package(const char* publisher, const char* package, int revision)
{
    int         status;
    char        name[128];
    const char* path;

    snprintf(&name[0], sizeof(name), "%s/%s", publisher, package);

    status = store_package_path(
        &(struct store_package) {
            .name = &name[0],
            .platform = CHEF_PLATFORM_STR,
            .arch = CHEF_ARCHITECTURE_STR,
            .channel = NULL,
            .revision = revision
        },
        &path
    );
    if (status) {
        VLOG_ERROR("served", "could not find the revision %i for %s/%s\n", revision, publisher, package);
        return status;
    }

    status = __verify_store_package(path, publisher, package, revision);
    if (status) {
        VLOG_ERROR("served", "could not verify the authenticity of the package %s of publisher %s\n", package, publisher);
        return status;
    }

    return 0;
}

int utils_verify_local_package(const char* packagePath, const char* proofPath, struct chef_package_proof** proofOut)
{
    struct chef_package_proof* proof = NULL;
    int                        status;
    VLOG_DEBUG("served", "utils_verify_local_package(package=%s, proof=%s)\n", packagePath, proofPath);

    status = utils_load_local_package_proof(proofPath, &proof);
    if (status) {
        VLOG_ERROR("served", "utils_verify_local_package: failed to load developer proof from path %s\n", proofPath);
        return status;
    }

    status = __verify_package_proof(proof, packagePath, CHEF_PACKAGE_PROOF_ORIGIN_DEVELOPER);
    if (status) {
        VLOG_ERROR("served", "utils_verify_local_package: failed to verify developer proof for package %s\n", packagePath);
        chef_package_proof_free(proof);
        return status;
    }

    if (proofOut != NULL) {
        *proofOut = proof;
    } else {
        chef_package_proof_free(proof);
    }
    return 0;
}
