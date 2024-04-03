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

#include <chef/api/package.h>
#include <chef/client.h>
#include <errno.h>
#include <gracht/client.h>
#include <chef/package.h>
#include <chef/platform.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <wolfssl/wolfcrypt/sha512.h>
#include <wolfssl/wolfcrypt/rsa.h>
#include <wolfssl/wolfcrypt/signature.h>

#include "chef_served_service_client.h"

#define __TEMPORARY_FILENAME ".cheftmpdl"

extern int __chef_client_initialize(gracht_client_t** clientOut);

static const char* g_publicKey = 
"-----BEGIN PUBLIC KEY-----"
"MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAuWpXSNzQqpfd7ZYHRWSh"
"j5Dfs49jaaUQkAzh+fH8ka5OEUA4yojauq7qX3lFWtmMnua/9XhY1x5xAC/zxDtb"
"gif09/FbN4rvSlZ0MIq4aC2bvECVCy/S3BtWZk5QHPsRExbMX16vJ8Cmhby6JV8X"
"0eCdk4nMGyDIktg697GqkpLeL4QeHNliPWLRuxNwHyguLUW3ch83gEQzdClrGSNj"
"aJiTx05QhUUscaIJGJH/LH2MuGaMwGyDzl0wLO1BhlTRzsPd6lxmQb3c9s92YPjS"
"sbYBfDFSBK2k3ACfD2+8bGZlu2NMobV4iGYlO26N2qypXZdV6/RLyxw2+aEx+IEY"
"pQIDAQAB"
"-----END PUBLIC KEY-----";

static const char* g_installMsgs[] = {
    "success",
    "verification failed, invalid or corrupt package",
    "package installation failed due to technical problems", // lol lets improve this some day, but view chef logs for details
    "package was installed but failed to load applications",
    "package was installed but failed to execute hooks, package is in undefined state"
};

static void __print_help(void)
{
    printf("Usage: serve install <pack> [options]\n");
    printf("Options:\n");
    printf("  -c, --channel\n");
    printf("      Install from a specific channel, default: stable\n");
    printf("  -h, --help\n");
    printf("      Print this help message\n");
}

static int __ask_yes_no_question(const char* question)
{
    char answer[3];
    printf("%s [y/n] ", question);
    if (fgets(answer, sizeof(answer), stdin) == NULL) {
        return 0;
    }
    return answer[0] == 'y' || answer[0] == 'Y';
}

static int __parse_package_identifier(const char* id, const char** publisherOut, const char** nameOut)
{
    char** names;
    int    count = 0;

    // split the publisher/package
    names = strsplit(id, '/');
    if (names == NULL) {
        fprintf(stderr, "unknown package name or path: %s\n", id);
        return -1;
    }
    
    while (names[count] != NULL) {
        count++;
    }

    if (count != 2) {
        fprintf(stderr, "unknown package name or path: %s\n", id);
        return -1;
    }

    *publisherOut = strdup(names[0]);
    *nameOut      = strdup(names[1]);
    strsplit_free(names);
    return 0;
}

#define _SEGMENT_SIZE (1024 * 1024)

static char* __calculate_sha512(const char* path)
{
    FILE*  file;
    char*  buffer;
    int    status;
    byte*  checksum;
    Sha512 sha;

    file = fopen(path, "rb");
    if (!file) {
        return NULL;
    }

    checksum = (byte*)malloc(SHA512_DIGEST_SIZE);
    if (checksum == NULL) {
        fclose(file);
        return NULL;
    }

    buffer = (char*)malloc(_SEGMENT_SIZE);
    if (buffer == NULL) {
        free(checksum);
        fclose(file);
        return NULL;
    }

    status = wc_InitSha512(&sha);
    if (status != 0) {
        free(checksum);
        free(buffer);
        fclose(file);
        return NULL;
    }

    status = 0;
    while (1) {
        size_t read;

        read = fread(buffer, 1, _SEGMENT_SIZE, file);
        if (read == 0) {
            break;
        }

        wc_Sha512Update(&sha, buffer, _SEGMENT_SIZE);

        // was it last segment?
        if (read < _SEGMENT_SIZE) {
            break;
        }
    }

    wc_Sha512Final(&sha, checksum);
    free(buffer);
    fclose(file);
    return (char*)checksum;
}

static int __verify_signature(const char* signature, const char* checksum)
{
    RsaKey rsaPublicKey;
    int    status;
    word32 idx = 0;
    
    // Import the public key
    wc_InitRsaKey(&rsaPublicKey, 0);
    wc_RsaPublicKeyDecode(g_publicKey, &idx, &rsaPublicKey, strlen(g_publicKey));

    // Perform signature verification using public key
    status = wc_SignatureVerifyHash(
        WC_HASH_TYPE_SHA512, WC_SIGNATURE_TYPE_RSA,
        checksum, 64,
        signature, 256,
        &rsaPublicKey, sizeof(rsaPublicKey)
    );
    wc_FreeRsaKey(&rsaPublicKey);
    return status;
}

static char* __load_signature(const char* path)
{
    FILE*  fd;
    long   size;
    size_t read;
    char*  signature;

    fd = fopen(path, "rb");
    if (fd == NULL) {
        fprintf(stderr, "failed to open package: %s\n", path);
        return NULL;
    }

    fseek(fd, 0, SEEK_END);
    size = ftell(fd);
    fseek(fd, size - 256, SEEK_SET);

    signature = malloc(256);
    if (signature == NULL) {
        fprintf(stderr, "failed to allocate signature buffer\n");
        fclose(fd);
        return NULL;
    }

    read = fread(signature, 1, 256, fd);
    fclose(fd);

    if (read != 256) {
        fprintf(stderr, "failed to read signature from package: %s\n", path);
        free(signature);
        return NULL;
    }
    return signature;
}

// load signature from package, it's the last 256 bytes
static int __verify_package_signature(const char* path, char** publisherOut)
{
    char* calculatedChecksum;
    char* signature;
    int   status;

    // calculate SHA512 of the package
    calculatedChecksum = __calculate_sha512(path);
    if (calculatedChecksum == NULL) {
        return -1;
    }

    // load signature
    signature = __load_signature(path);
    if (signature == NULL) {
        free(calculatedChecksum);
        return -1;
    }

    // decrypt signature
    status = __verify_signature(signature, calculatedChecksum);
    if (status) {
        fprintf(stderr, "signature verification failed\n");
        free(calculatedChecksum);
        free(signature);
        return -1;
    }

    // use the hash to lookup package

    // retrieve package info
    return 0;
}

static char* __get_unsafe_infoname(struct chef_package* package, struct chef_version* version)
{
    char* name;

    name = malloc(128);
    if (name == NULL) {
        return NULL;
    }

    sprintf(name, "[devel] %s %i.%i.%i",
        package->package, version->major,
        version->minor, version->patch
    );
    return name;
}

static char* __get_safe_infoname(char* publisher, struct chef_package* package, struct chef_version* version)
{
    char* name;

    name = malloc(128);
    if (name == NULL) {
        return NULL;
    }

    sprintf(name, "%s/%s (verified, revision %i)",
        publisher, package->package, version->revision
    );
    return name;
}

static int __verify_package(const char* path, char** infoNameOut, char** publisherOut)
{
    struct chef_package* package;
    struct chef_version* version;
    int                  status;

    // dont care about commands
    status = chef_package_load(path, &package, &version, NULL, NULL);
    if (status != 0) {
        fprintf(stderr, "failed to load package: %s\n", path);
        return -1;
    }

    // verify revision being non-zero, otherwise we need to warn about
    // the package being a development package
    if (version->revision == 0) {
        fprintf(stderr, "warning: package is a development package, which means chef cannot "
                        "verify its integrity.\n");
        status = __ask_yes_no_question("continue?");
        if (!status) {
            fprintf(stderr, "aborting\n");
            chef_package_free(package);
            chef_version_free(version);
            return -1;
        }

        *infoNameOut  = __get_unsafe_infoname(package, version);
        *publisherOut = strdup("unverified");
    } else {
        // verify package signature
        status = __verify_package_signature(path, publisherOut);
        if (status != 0) {
            fprintf(stderr, "failed to verify package signature\n");
            chef_package_free(package);
            chef_version_free(version);
            return -1;
        }

        *infoNameOut  = __get_safe_infoname(*publisherOut, package, version);
    }

    // free resources
    chef_package_free(package);
    chef_version_free(version);
    return 0;
}

static void __cleanup(void)
{
    platform_unlink(__TEMPORARY_FILENAME);
}

int install_main(int argc, char** argv)
{
    gracht_client_t*            client;
    int                         status;
    struct platform_stat        stats;
    struct chef_download_params params    = { 0 };
    const char*                 package   = NULL;
    char*                       fullpath  = NULL;
    char*                       publisher = NULL;
    char*                       infoName  = NULL;

    // set default channel
    params.channel = "stable";

    if (argc > 2) {
        for (int i = 2; i < argc; i++) {
            if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
                __print_help();
                return 0;
            } else if (!strncmp(argv[i], "-c", 2) || !strncmp(argv[i], "--channel", 9)) { 
                char* channel = strchr(argv[i], '=');
                if (channel) {
                    channel++;
                    params.channel = channel;
                } else {
                    printf("missing recipe name for --channel=...\n");
                    return -1;
                }
            } else if (argv[i][0] != '-') {
                if (package == NULL) {
                    package = argv[i];
                } else {
                    printf("unknown option: %s\n", argv[i]);
                    __print_help();
                    return -1;
                }
            }
        }
    }

    if (package == NULL) {
        printf("no package specified for install\n");
        __print_help();
        return -1;
    }

    status = chefclient_initialize();
    if (status != 0) {
        fprintf(stderr, "failed to initialize chef client\n");
        return -1;
    }
    atexit(chefclient_cleanup);

    // is the package a path? otherwise try to download from
    // official repo
    if (platform_stat(package, &stats)) {
        status = __parse_package_identifier(package, &params.publisher, &params.package);
        if (status != 0) {
            return status;
        }

        // store publisher and the informational name
        publisher = strdup(params.publisher);
        infoName  = strdup(package);

        // we only allow installs from native packages
        params.platform = CHEF_PLATFORM_STR;
        params.arch     = CHEF_ARCHITECTURE_STR;

        // cleanup the file on exit
        atexit(__cleanup);

        printf("downloading package %s from channel %s\n", package, params.channel);
        status = chefclient_pack_download(&params, __TEMPORARY_FILENAME);
        if (status != 0) {
            printf("failed to download package: %s\n", strerror(status));
            return status;
        }

        package = __TEMPORARY_FILENAME;
    } else {
        status = __verify_package(package, &infoName, &publisher);
        if (status != 0) {
            return status;
        }
    }

    // at this point package points to a file in our PATH
    // but we need the absolute path
    fullpath = platform_abspath(package);
    if (fullpath == NULL) {
        printf("failed to get resolve package path: %s\n", package);
        return -1;
    }

    status = __chef_client_initialize(&client);
    if (status != 0) {
        printf("failed to initialize client: %s\n", strerror(status));
        return status;
    }

    printf("installing %s\n", infoName);
    status = chef_served_install(client, NULL, publisher, fullpath);
    if (status != 0) {
        printf("communication error: %i\n", status);
        goto cleanup;
    }

    gracht_client_wait_message(client, NULL, GRACHT_MESSAGE_BLOCK);

cleanup:
    gracht_client_shutdown(client);
    free((void*)params.publisher);
    free((void*)params.package);
    free(infoName);
    free(fullpath);
    free(publisher);
    return status;
}

void chef_served_event_package_installed_invocation(gracht_client_t* client, const enum chef_install_status status, const struct chef_served_package* info)
{
    printf("installation status: %s\n", g_installMsgs[status]);
    if (status == CHEF_INSTALL_STATUS_SUCCESS) {
        // print package info
    }
}
