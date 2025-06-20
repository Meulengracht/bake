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
 * Package System TODOs:
 * - api-keys
 * - pack deletion
 */
#define _GNU_SOURCE

#include <errno.h>
#include <chef/config.h>
#include <chef/dirs.h>
#include <chef/platform.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vlog.h>

#define __DEFAULT_LOCAL_CONNECTION_STRING "unix:@/chef/waiterd/api"

static int __ask_yes_no_question(const char* question)
{
    char answer[3] = { 0 };
    printf("%s (default=no) [Y/n] ", question);
    if (fgets(answer, sizeof(answer), stdin) == NULL) {
        return 0;
    }
    return answer[0] == 'Y';
}

static char* __ask_question(const char* question, const char* defaultAnswer)
{
    char answer[512] = { 0 };
    printf("%s (default=%s) ", question, defaultAnswer);
    if (fgets(answer, sizeof(answer), stdin) == NULL) {
        return 0;
    }
    if (answer[0] == '\n') {
        strcpy(&answer[0], defaultAnswer);
    } else {
        for (int i = 0; answer[i] != 0; i++) {
            if (answer[i] == '\n') {
                answer[i] = 0;
                break;
            }
        }
    }
    return platform_strdup(&answer[0]);
}

static int __validate_connection_string(const char* connectionString)
{
    if (!strncmp(connectionString, "unix:", 5)) {
        return 0;
    } else if (!strncmp(connectionString, "inet4:", 6)) {
        return 0;
    }
    fprintf(stderr, "bake: unsupported protocol in connection string");
    return -1;
}

static int __parse_unix_string(struct chef_config_address* address, const char* path)
{
    VLOG_DEBUG("remote", "__parse_unix_string(path=%s)\n", path);
    
    address->type = "local";
    address->address = path;
    address->port = 0;
    
    return 0;
}

// modifies the ip string
static int __parse_inet4_string(struct chef_config_address* address, char* ip)
{
    char* split;
    VLOG_DEBUG("remote", "__parse_inet4_string(ip=%s)\n", ip);

    split = strchr(ip, ':');
    if (split == NULL) {
        fprintf(stderr, "bake: ip4 address must specify a port (%s)\n", ip);
        return -1;
    }

    *split = '\0';
    split++;

    address->type = "inet4";
    address->address = ip;
    address->port = atoi(split);
    return 0;
}

// may modify the connectionString
static int __parse_connection_string(struct chef_config_address* address, char* connectionString)
{
    char* split;
    VLOG_DEBUG("remote", "__parse_connection_string(conn=%s)\n", connectionString);

    // split at the ':'
    split = strchr(connectionString, ':');
    split++;

    if (!strncmp(connectionString, "unix:", 5)) {
        return __parse_unix_string(address, split);
    } else if (!strncmp(connectionString, "inet4:", 6)) {
        return __parse_inet4_string(address, split);
    }
    return -1;
}

// may modify the connectionString
static int __write_configuration(char* connectionString)
{
    struct chef_config*        config;
    struct chef_config_address address;
    int                        status;
    VLOG_DEBUG("remote", "__write_configuration(conn=%s)\n", connectionString);

    status = __parse_connection_string(&address, connectionString);
    if (status) {
        fprintf(stderr, "bake: failed to parse connection string %s\n", connectionString);
        return status;
    }

    config = chef_config_load(chef_dirs_config());
    if (config == NULL) {
        fprintf(stderr, "bake: failed to load configuration\n");
        return -1;
    }

    chef_config_set_remote_address(config, &address);
    
    status = chef_config_save(config);
    if (status) {
        fprintf(stderr, "bake: failed to write new configuration\n");
        return status;
    }
    return 0;
}

int remote_local_init_default(void)
{
    int   status;
    char* copy = platform_strdup(__DEFAULT_LOCAL_CONNECTION_STRING);
    if (copy == NULL) {
        return -1;
    }

    status = __write_configuration(copy);
    free(copy);
    return status;
}

int remote_wizard_init(void)
{
    char* connectionString = NULL;
    int   status;
    
    printf("Welcome to the remote build initialization wizard!\n");
    printf("This will guide you through the neccessary setup to\n");
    printf("enable remote builds on your local machine.\n");
    printf("Before we get started, you must have a computer\n");
    printf("setup with the waiterd/cookd software, and have their\n");
    printf("connection strings ready.\n");
    printf("Examples:\n");
    printf(" - unix:/my/path\n");
    printf(" - inet4:192.6.4.1:9202\n");
    printf("\n");
    
    connectionString = __ask_question(
        "please enter the address of the waiterd daemon",
        __DEFAULT_LOCAL_CONNECTION_STRING
    );
    if (__validate_connection_string(connectionString)) {
        free(connectionString);
        return -1;
    }

    status = __ask_yes_no_question(
        "this will update the current configuration of bake, are you sure?"
    );
    if (!status) {
        fprintf(stderr, "bake: aborting\n");
        free(connectionString);
        return -1;
    }

    status = __write_configuration(connectionString);
    free(connectionString);
    return status;
}
