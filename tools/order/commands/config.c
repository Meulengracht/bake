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
#include <chef/config.h>
#include <chef/dirs.h>
#include <chef/platform.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static void __print_help(void)
{
    printf("Usage: order config <param> <value>\n");
    printf("Examples:\n");
    printf("  order config auth.name  <\"Your Name\">\n");
    printf("  order config auth.email <email>\n");
    printf("  order config auth.key   <path-to-key-file>\n");
    printf("Options:\n");
    printf("  -h, --help\n");
    printf("      Print this help message\n");
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

static int __handle_option(const char* option, const char* value)
{
    struct chef_config* config;
    void*               accountSection;

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

    if (strcmp(option, "auth.key") == 0) {
        if (value == NULL) {
            printf("auth.key = %s\n", (const char*)chef_config_get_string(config, accountSection, "private-key"));
        } else {
            // validate that the file exists
            struct platform_stat st;
            char                 tmp[PATH_MAX];

            snprintf(
                &tmp[0], sizeof(tmp) -1,
                "%s.pub",
                value
            );

            if (platform_stat(value, &st) || st.type != PLATFORM_FILETYPE_FILE) {
                fprintf(stderr, "order: specified key file '%s' does not exist: %s\n", value, strerror(errno));
                return -1;
            }

            if (platform_stat(&tmp[0], &st) || st.type != PLATFORM_FILETYPE_FILE) {
                fprintf(stderr, "order: specified key file '%s' does not exist: %s\n", value, strerror(errno));
                return -1;
            }

            chef_config_set_string(config, accountSection, "private-key", value);
            chef_config_set_string(config, accountSection, "public-key", &tmp[0]);
        }
    } else if (strcmp(option, "auth.name") == 0) {
        if (value == NULL) {
            printf("auth.name = %s\n", (const char*)chef_config_get_string(config, accountSection, "name"));
        } else {
            chef_config_set_string(config, accountSection, "name", value);
        }
    } else if (strcmp(option, "auth.email") == 0) {
        if (value == NULL) {
            printf("auth.email = %s\n", (const char*)chef_config_get_string(config, accountSection, "email"));
        } else {
            if (__verify_email(value)) {
                fprintf(stderr, "order: invalid email provided\n");
                return -1;
            }
            chef_config_set_string(config, accountSection, "email", value);
        }
    } else {
        fprintf(stderr, "order: unknown option '%s' for 'config'\n", option);
        return -1;
    }

    chef_config_save(config);
    return 0;
}

int config_main(int argc, char** argv)
{
    const char* option = NULL;
    const char* value  = NULL;
    int         status;

    if (argc > 2) {
        for (int i = 2; i < argc; i++) {
            if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
                __print_help();
                return 0;
            } else if (option == NULL) {
                option = argv[i];
            } else if (value == NULL) {
                value = argv[i];
            }
        }
    }

    if (option == NULL) {
        fprintf(stderr, "order: missing option\n");
        __print_help();
        return -1;
    }
    return __handle_option(option, value);
}
