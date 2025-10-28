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

#include <chef/fridge.h>
#include <chef/platform.h>
#include <errno.h>
#include <openssl/decoder.h>
#include <openssl/err.h>
#include <openssl/x509.h>
#include <openssl/pem.h>

const char* g_certAuthority = "MIIF1TCCA72gAwIBAgIUBrKWdEkac/ETLHLvzNjz4e6mElgwDQYJKoZIhvcNAQENBQAwejELMAkGA1UEBhMCREsxEzARBgNVBAgMCkNvcGVuaGFnZW4xEzARBgNVBAcMCkNvcGVuaGFnZW4xDTALBgNVBAoMBENoZWYxDjAMBgNVBAsMBVN0b3JlMSIwIAYDVQQDDBlDaGVmIFN0b3JlIFJvb3QgQXV0aG9yaXR5MB4XDTI1MTAyNzA4NTIyNVoXDTM1MTAyNTA4NTIyNVowejELMAkGA1UEBhMCREsxEzARBgNVBAgMCkNvcGVuaGFnZW4xEzARBgNVBAcMCkNvcGVuaGFnZW4xDTALBgNVBAoMBENoZWYxDjAMBgNVBAsMBVN0b3JlMSIwIAYDVQQDDBlDaGVmIFN0b3JlIFJvb3QgQXV0aG9yaXR5MIICIjANBgkqhkiG9w0BAQEFAAOCAg8AMIICCgKCAgEAwGnBxbYyRxTQX8+ENMDQMFK8XuMVlCoE1/wcHxseBGLOAEV6FqKdmw8daIf7dqkpK9dVyRm5MYAe1DaDvSWXPOZtzpklWUzLTkYIX+K81QTDgF58W4OkCz9qQnhVJ9snPgjy6UL/9mNJ4g4OUWtQiqkpZsua9J75p3aUQjeM1dOtAUsIps8dGyOJ75Z1h9yTGomNt9xK95I56x1vru5ifKvUsZ5iKpA9uXQ+VZIxlfDwCjl+p3wH0H1ZgvjIk1etdzWOls0E2KNycjGwyQ+H/bJtQZ4oEaZNETRu5QuXJ4zUxdjt7HnUZWD04ySIIT4CyiaH8Lgo6oXIJkal9cJQYgf5kZk2OWhelu1DcqZhOc7GDPU1PYFh8riy2LKxhl6GCVaUgOPeQzB3TLP/Doa6ME9xczOCOlJKrR0aQRgcJSKQss6N8Zrxy3xjnKkAV8YxUu317onv4JTxLyyzJdn3HjoGaQLM9CHh0IbfUJPRIPERJn3L2FGnWlA+lFD2uj1qTfAdOxElRrdLWTFzYHEM+RgBkzOU7hLUNpFsK+IY1zCu+7xtQXwdWqcLM0ppDQZwayMDB/9HfIY7+yOcYQg3nO0Yyi5Yik9mhTah4e2svjYzwEGSIu/SyASipXULf1RY+0FRlDhHcnjGu6oZURjEim6BZcU4LsVpmOlyOAFcl+MCAwEAAaNTMFEwHQYDVR0OBBYEFIRJLeleZKj9FAGU3ojpbCi/X+f8MB8GA1UdIwQYMBaAFIRJLeleZKj9FAGU3ojpbCi/X+f8MA8GA1UdEwEB/wQFMAMBAf8wDQYJKoZIhvcNAQENBQADggIBAA+RIVZ0+O7WGqMuzu5QdiTNAa3pTSh9YUSWj0K8VwgxNkOC+2xovVujkBSTSrcXpFElNUbhsPcoMWFSoy/4IjhyGsNeNDXESzPgND+AWsZIQQH3zhOimBN4ulDBqkjgY/t37M3e1g9g6/p1/n47h/KlOMpi3qiAj3DmsmIfsVb0BtkC4XFP3+z4BpqTGOnS6a741MvPLyYAYij26rmbt56jm8Wn1wGSfmtZ7UatIxDgopO65ZLKrWeQiw6elB/Rvw2IY/izqy4XPRYlgfGjrWvf0BX2IJ+l3PfmKYwELlMFIeLCwJj0v3NAUGuJRNue65lmeMWJhkNIRSNHs0KdlUpnuO65ytOFP0Z/3zj2dDevcwXwfQUVtJ2css06S5Rr7wbVouZptXGFoH4dFz6EDE8GvJvmdmv0EJgYKKYLcy+7PSl7bqZIt8loboHFvBF45KtpChxHk+/0pmPcBVApo12F6JQ7dsL9RD+BHvDQygx3S1ovQMeLKeYboZ6pN4TbItMR3gaLDAnEZ6/pDqK1mNdxmU62KEcVQ46fy7b087Q8I4yh2u7b/xMeyx80dXR85rcbHsWywWO5dFTB0kqZIzKyXrHEDGGlyltu57YlZ7iRChqu6MAHztHZDs0SisZwMbFz5HZeTDAKtmGrMJdN3VQd/Or2tEFdcjCeU4fR8ygM";

#define _SEGMENT_SIZE (1024 * 1024)

static void __print_crypt_errors(const char* prefix)
{
    int status;
    char tmp[256];
    while ((status = ERR_get_error()) != 0) {
        ERR_error_string_n(status, tmp, sizeof(tmp));
        fprintf(stderr, "%s: %s\n", prefix, &tmp[0]);
    }
}

static int __verify_signature(EVP_PKEY* key, const char* data, size_t dataLength, unsigned char* signature, size_t signaturelength) {
    EVP_MD_CTX* mdctx;
    int         status, r;

    mdctx = EVP_MD_CTX_create();
    if (mdctx == NULL) {
        fprintf(stderr, "failed to create decoder context\n");
        return -1;
    }

    status = EVP_DigestVerifyInit(mdctx, NULL, EVP_sha512(), NULL, key);
    if (status != 1) {
        fprintf(stderr, "failed to initialize digest context\n");
        EVP_MD_CTX_free(mdctx);
        return -1;
    }

    status = EVP_DigestVerifyUpdate(mdctx, data, dataLength);
    if (status != 1) {
        fprintf(stderr, "failed to process digest data\n");
        EVP_MD_CTX_free(mdctx);
        return -1;
    }

    r = EVP_DigestVerifyFinal(mdctx, signature, signaturelength);
    if (r == 1) {
        status = 0;
    } else {
        if (r != 0) {
            __print_crypt_errors("failed to verify proof signature");
        }
        status = -1;
    }
    EVP_MD_CTX_free(mdctx);
    return 0;
}

static int __verify_signature_against_cert(struct fridge_proof_publisher* proof)
{
    BIO*      keybio;
    X509*     xcert;
    EVP_PKEY* pubkey;
    int       status;

    keybio = BIO_new_mem_buf(g_certAuthority, strlen(g_certAuthority));
    if (keybio == NULL) {
        VLOG_ERROR("served", "__verify_signature_against_cert: failed to create memory reader\n");
        return -1;
    }

    xcert = PEM_read_bio_X509(keybio, &xcert, NULL, NULL);
    if (xcert == NULL) {
        VLOG_ERROR("served", "__verify_signature_against_cert: failed to read certificate: %s\n", ERR_error_string(ERR_get_error(), NULL));
        BIO_free_all(keybio);
        return -1;
    }

    pubkey = X509_get_pubkey(xcert);
    if (xcert == NULL) {
        VLOG_ERROR("served", "__verify_signature_against_cert: failed to read public key from certificate: %s\n", ERR_error_string(ERR_get_error(), NULL));
        X509_free(xcert);
        BIO_free_all(keybio);
        return -1;
    }

    status = __verify_signature(
        pubkey,
        &proof->public_key[0],
        strlen(&proof->public_key[0]),
        &proof->signed_key[0],
        strlen(&proof->signed_key[0])
    );
    if (status) {
        VLOG_ERROR("served", "__verify_signature_against_cert: failed to verify proof against certificate: %s\n", ERR_error_string(ERR_get_error(), NULL));
    }

    EVP_PKEY_free(pubkey);
    X509_free(xcert);
    BIO_free_all(keybio);
    return 0;
}

static int __verify_and_get_publisher_key(const char* publisher, struct fridge_proof_publisher* proof)
{
    int  status;
    char key[128];

    proof_format_publisher_key(&key[0], sizeof(key), publisher);

    status = fridge_proof_lookup(FRIDGE_PROOF_PUBLISHER, &key[0], proof);
    if (status) {
        VLOG_ERROR("served", "__verify_publisher_key: failed to retrieve the proof of publisher %s\n", publisher);
        return status;
    }
    return __verify_signature_against_cert(proof);
}

static int __calculate_file_sha512(const char* path, unsigned char** hash, unsigned int* hashLength)
{
    FILE*       file;
    char*       buffer;
    int         status;
    EVP_MD_CTX* mdctx;

    file = fopen(path, "rb");
    if (!file) {
        return -1;
    }

    buffer = (char*)malloc(_SEGMENT_SIZE);
    if (buffer == NULL) {
        fclose(file);
        return -1;
    }

    mdctx = EVP_MD_CTX_new();
    if (mdctx == NULL) {
        fprintf(stderr, "failed to allocate SHA512 context\n");
        status = -1;
        goto cleanup;
    }
    
    status = EVP_DigestInit_ex(mdctx, EVP_sha512(), NULL);
    if (status != 1) {
        __print_crypt_errors("failed to calculate SHA512");
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
        if (status != 1) {
            __print_crypt_errors("failed to process SHA512 data");
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
        fprintf(stderr, "failed to allocate memory for SHA512 checksum\n");
        status = -1;
        goto cleanup;
    }

    status = EVP_DigestFinal_ex(mdctx, *hash, hashLength);
    if (status != 1) {
        __print_crypt_errors("failed to finalize SHA512 data");
        status = -1;
    }

cleanup:
    free(buffer);
    fclose(file);
    EVP_MD_CTX_free(mdctx);
    return status;
}


static int __parse_public_key(const unsigned char* key, size_t keyLength, EVP_PKEY** keyOut) {
    int               status;
    OSSL_DECODER_CTX* dctx = OSSL_DECODER_CTX_new_for_pkey(
        keyOut,
        "DER",
        NULL,
        "RSA",
        OSSL_KEYMGMT_SELECT_PUBLIC_KEY,
        NULL,
        NULL
    );
    if (dctx == NULL) {
        fprintf(stderr, "failed to create key context\n");
        return -1;
    }

    status = OSSL_DECODER_from_data(dctx, &key, &keyLength);
    OSSL_DECODER_CTX_free(dctx);
    if (!status) {
        fprintf(stderr, "failed to decode key for verification\n");
        return -1;
    }
    return 0;
}

static int __verify_package(struct fridge_proof_publisher* publisherProof, const char* packagePath, const char* publisher, const char* package, int revision)
{
    unsigned char*              hash;
    unsigned int                hashLength;
    int                         status;
    char                        key[128];
    struct fridge_proof_package proof;
    EVP_PKEY*                   pkey;

    proof_format_package_key(&key[0], sizeof(key), publisher, package, revision);

    status = fridge_proof_lookup(FRIDGE_PROOF_PACKAGE, &key[0], &proof);
    if (status) {
        VLOG_ERROR("served", "__verify_publisher_key: failed to retrieve the proof for package %s\n", &key[0]);
        return status;
    }

    status = __calculate_file_sha512(packagePath, &hash, &hashLength);
    if (status) {
        VLOG_ERROR("served", "__verify_publisher_key: failed to calculate SHA512 checksum of package path %s\n", packagePath);
        return status;
    }

    status = __parse_public_key(&publisherProof->public_key[0], strlen(&publisherProof->public_key[0]), &pkey);
    if (status) {
        VLOG_ERROR("served", "__verify_publisher_key: failed to parse public key from data %s\n", &publisherProof->public_key[0]);
        return status;
    }

    status = __verify_signature(pkey, hash, hashLength, &proof.signature[0], strlen(&proof.signature[0]));
    if (status) {
        VLOG_ERROR("served", "__verify_signature_against_cert: failed to verify proof against publisher %s\n", publisher);
    }
    return status;
}

int served_verify_publisher(const char* publisher)
{
    int                           status;
    struct fridge_proof_publisher publisherProof;

    status = __verify_and_get_publisher_key(publisher, &publisherProof);
    if (status) {
        VLOG_ERROR("served", "could not verify the authenticity of the publisher %s\n", publisher);
        return status;
    }

    return 0;
}

int served_verify_package(const char* publisher, const char* package, int revision)
{
    int                           status;
    char                          name[128];
    const char*                   path;
    struct fridge_proof_publisher publisherProof;

    snprintf(&name[0], sizeof(name), "%s/%s", publisher, package);

    status = fridge_package_path(&(struct fridge_package) {
        .name = &name[0],
        .platform = CHEF_PLATFORM_STR,
        .arch = CHEF_ARCHITECTURE_STR,
        .channel = NULL,
        .revision = revision
    }, &path);
    if (status) {
        VLOG_ERROR("served", "could not find the revision %i for %s/%s\n", revision, publisher, package);
        return status;
    }

    status = __verify_and_get_publisher_key(publisher, &publisherProof);
    if (status) {
        VLOG_ERROR("served", "could not verify the authenticity of the publisher %s\n", publisher);
        return status;
    }

    status = __verify_package(&publisherProof, path, publisher, package, revision);
    if (status) {
        VLOG_ERROR("served", "could not verify the authenticity of the package %s of publisher %s\n", package, publisher);
        return status;
    }

    return 0;
}
