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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int __ask_yes_no_question(const char* question)
{
    char answer[3];
    printf("%s [y/n] ", question);
    if (fgets(answer, sizeof(answer), stdin) == NULL) {
        return 0;
    }
    return answer[0] == 'y' || answer[0] == 'Y';
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

void account_setup(void)
{
    struct chef_account* account        = NULL;
    char*                publisherName  = NULL;
    char*                publisherEmail = NULL;
    int                  success;

    success = __ask_yes_no_question("Do you want to setup an account now?");
    if (!success) {
        return;
    }
    
    // allocate memory for the account
    account = chef_account_new();
    if (account == NULL) {
        fprintf(stderr, "failed to allocate memory for the account\n");
        return;
    }
    
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

    // update account members
    chef_account_set_publisher_name(account, publisherName);
    chef_account_set_publisher_email(account, publisherEmail);
    
    printf("Setting up account...\n");
    success = chef_account_update(account);
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
    chef_account_free(account);
}
