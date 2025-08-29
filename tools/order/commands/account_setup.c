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

#include <errno.h>
#include <chef/api/account.h>
#include <chef/client.h>
#include <chef/config.h>
#include <chef/dirs.h>
#include <chef/platform.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int __ask_yes_no_question(const char* question)
{
    char answer[3];
    printf("%s [Y/n] ", question);
    if (fgets(answer, sizeof(answer), stdin) == NULL) {
        return 0;
    }
    return answer[0] == 'Y';
}

static char* __ask_input_question(const char* question)
{
    char*  buffer     = NULL;
    size_t bufferSize = 0;
    int    written    = 0;
    int    ch;
    
    // ask the question
    printf("%s", question);

    // retrieve the answer
    while ((ch = getchar()) != '\n' && ch != EOF) {
        if (bufferSize == 0) {
            bufferSize = 128;
            buffer = malloc(bufferSize);
            if (buffer == NULL) {
                return NULL;
            }
        } else if (written == bufferSize - 1) {
            bufferSize *= 2;
            buffer = realloc(buffer, bufferSize);
            if (buffer == NULL) {
                return NULL;
            }
        } 
        
        if (ch == '\b') {
            if (written > 0) {
                buffer[--written] = '\0';
            }
        } else {
            buffer[written++] = (char)ch;
        }
    }
    buffer[written] = '\0';
    return buffer;
}

static int __verify_publisher_name(const char* name)
{
    int i = 0;

    if (name == NULL || strlen(name) < 3 || strlen(name) > 63) {
        fprintf(stderr, "publisher name must be between 3-63 characters\n");
        return -1;
    }

    // verify name is only containing allowed characters
    while (name[i]) {
        if (!(name[i] >= 'a' && name[i] <= 'z') &&
            !(name[i] >= 'A' && name[i] <= 'Z') &&
            !(name[i] >= '0' && name[i] <= '9') && name[i] != '-') {
            fprintf(stderr, "publisher name must only contain characters [a-zA-Z0-9-]\n");
            return -1;
        }
        i++;
    }
    return 0;
}

static int __verify_email(const char* email)
{
    int atFound  = 0;
    int dotFound = 0;
    int i        = 0;

    if (email == NULL) {
        return -1;
    }

    // verify email contains @ and atleast one .
    while (email[i]) {
        if (email[i] == '@') {
            atFound = 1;
        } else if (email[i] == '.') {
            dotFound = 1;
        }
        i++;
    }
    
    return (atFound != 0 && dotFound != 0) ? 0 : -1;
}

static char* __get_chef_directory(void)
{
    char   dir[PATH_MAX];
    int    status;

    status = platform_getuserdir(&dir[0], PATH_MAX);
    if (status) {
        fprintf(stderr, "order: failed to get user home directory: %s\n", strerror(errno));
        return NULL;
    }
    return strpathcombine(&dir[0], ".chef");
}

int account_login_setup(void)
{
    struct chef_account* account        = NULL;
    char*                publicKeyPath  = NULL;
    char*                privateKeyPath = NULL;
    int                  success;
    struct chef_config*  config;
    void*                accountSection;

    config = chef_config_load(chef_dirs_config());
    if (config == NULL) {
        fprintf(stderr, "order: failed to load configuration: %s\n", strerror(errno));
        return -1;
    }

    accountSection = chef_config_section(config, "account");
    if (accountSection == NULL) {
        fprintf(stderr, "order: failed to load account section from configuration: %s\n", strerror(errno));
        return -1;
    }

    publicKeyPath = (char*)chef_config_get_string(config, accountSection, "public-key");
    privateKeyPath = (char*)chef_config_get_string(config, accountSection, "private-key");
    if (publicKeyPath != NULL && privateKeyPath != NULL) {
        struct platform_stat st;
        int                  okay = 0;

        if (platform_stat(publicKeyPath, &st) || st.type != PLATFORM_FILETYPE_FILE) {
            fprintf(stderr, "order: configured public key file was invalid, reconfigure required.\n");
            okay = -1;
        }
        if (platform_stat(privateKeyPath, &st) | st.type != PLATFORM_FILETYPE_FILE) {
            fprintf(stderr, "order: configured private key file was invalid, reconfigure required.\n");
            okay = -1;
        }

        if (okay == 0) {
            publicKeyPath = platform_strdup(publicKeyPath);
            privateKeyPath = platform_strdup(privateKeyPath);
            goto login;
        }
    }

    printf("No account information found. An account is required to publish packages.\n");
    success = __ask_yes_no_question("Do you want to setup an account now?");
    if (!success) {
        return -1;
    }

    printf("\nChef accounts operate using RSA public/private keypairs.\n");
    printf("If you do not have a keypair, one will be generated for you.\n");
    printf("The private key will be stored on your local machine, and the public key\n");
    printf("will be uploaded to your account.\n");
    success = __ask_yes_no_question("Do you want to continue?");
    if (!success) {
        return -1;
    }

    // Allow the user to specify an existing keypair, or generate a new one.
    // The keypair must be able to sign messages using RSA-SHA256.
    printf("\nDo you want chef to generate a new key-pair for you?\n");
    printf("If you don't, you can configure which key should be used by executing\n\n");
    printf("   order config auth.key <path-to-private-key>\n\n");
    success = __ask_yes_no_question("Continue with keypair generation?");
    if (!success) {
        success = -1;
        goto cleanup;
    } else {
        char* dir = __get_chef_directory();
        if (dir == NULL) {
            goto cleanup;
        }

        success = pubkey_generate_rsa_keypair(2048, dir, &publicKeyPath, &privateKeyPath);
        free(dir);

        if (success) {
            fprintf(stderr, "failed to generate RSA keypair: %s\n", strerror(errno));
            goto cleanup;
        }

        printf("\nA new RSA keypair has been generated for you.\n");
        printf("Public key: %s\n", publicKeyPath);
        printf("Private key: %s\n", privateKeyPath);
        printf("\nPlease back up your private key, as it will be required to publish packages.\n");
        printf("The private key will not be uploaded to your account.\n");
    }

    // save the key paths to the config
    chef_config_set_string(config, accountSection, "public-key", publicKeyPath);
    chef_config_set_string(config, accountSection, "private-key", privateKeyPath);
    chef_config_save(config);

login:
    success = chefclient_login(&(struct chefclient_login_params) {
        .flow = CHEF_LOGIN_FLOW_TYPE_PUBLIC_KEY,
        .public_key = publicKeyPath,
        .private_key = privateKeyPath,
    });
    if (success) {
        fprintf(stderr, "order: failed to login with RSA keypair: %s\n", strerror(errno));
        goto cleanup;
    }

cleanup:
    free(publicKeyPath);
    free(privateKeyPath);
    return success;
}

void account_publish_setup(void)
{
    char* publisherName  = NULL;
    char* publisherEmail = NULL;
    int   success;

    // ask for the publisher name
    printf("We need to know the name under which your packages will be published. (i.e my-org)\n");
    printf("Please only include the name, characters allowed: [a-zA-Z0-9-], length must be between 3-63 characters\n");
    publisherName = __ask_input_question("Your publisher name: ");

    // check if the publisher name is valid
    if (__verify_publisher_name(publisherName) != 0) {
        goto cleanup;
    }

    // ask for the publisher email
    printf("Please provide an email which will be used for publisher name verification.\n");
    publisherEmail = __ask_input_question("Your publisher email: ");
    if (__verify_email(publisherEmail)) {
        fprintf(stderr, "Invalid email provided\n");
        goto cleanup;
    }

    printf("Setting up account...\n");
    success = chef_account_publisher_register(publisherName, publisherEmail);
    if (success != 0) {
        fprintf(stderr, "failed to setup account: %s\n", strerror(errno));
    } else {
        printf("Account setup complete!\n");
        printf("An email will be sent once your publisher name has been verified.\n");
        printf("We usually review your account within 24 hours, and remember to check your spam filter.\n");
    }

cleanup:
    free(publisherName);
    free(publisherEmail);
}
