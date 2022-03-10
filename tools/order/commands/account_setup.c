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

#include <errno.h>
#include <chef/account.h>
#include <chef/client.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int __ask_yes_no_question(const char* question)
{
    char answer[3];
    printf("%s [y/n] ", question);
    fgets(answer, sizeof(answer), stdin);
    return answer[0] == 'y' || answer[0] == 'Y';
}

static char* __ask_input_question(const char* question)
{
    char*  buffer = NULL;
    size_t bufferSize = 0;
    int    read;
    int    written;
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
        }
        else if (bufferSize == bufferSize - 1) {
            bufferSize *= 2;
            buffer = realloc(buffer, bufferSize);
            if (buffer == NULL) {
                return NULL;
            }
        }
        buffer[read++] = ch;
    }
    buffer[read] = '\0';
    return buffer;
}

void account_setup(void)
{
    struct chef_account* account;
    char*                publisherName;
    int                  publisherNameChanged;
    int                  success;

    success = __ask_yes_no_question("Do you want to setup an account now?");
    if (!success) {
        return;
    }
    
    // allocate memory for the account
    account = chef_account_new();
    if (account == NULL) {
        fprintf(stderr, "order: failed to allocate memory for the account\n");
        return;
    }
    
    // ask for the publisher name
    printf("We need to know the name under which your packages will be published. (i.e my-org)\n");
    printf("Please only include the name, characters allowed: [a-zA-Z0-9_-], length must be between 3-63 characters\n");
    publisherName = __ask_input_question("Your publisher name: ");
    if (publisherName == NULL) {
        return;
    }

    // check if the publisher name is valid
    if (strlen(publisherName) < 3 || strlen(publisherName) > 63) {
        fprintf(stderr, "order: publisher name must be between 3-63 characters\n");
        free(publisherName);
        chef_account_free(account);
        return;
    }

    // update account members
    chef_account_set_publisher_name(account, publisherName);
    free(publisherName);
    
    printf("Setting up account...\n");
    success = chef_account_update(account);

    chef_account_free(account);
    printf("Account setup complete.\n");
}
