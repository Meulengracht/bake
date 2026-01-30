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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void __print_help(void)
{
    printf("Usage: serve config <param> [value]\n");
    printf("\n");
    printf("Pack network settings:\n");
    printf("  serve config pack.<publisher>/<package>.network.gateway <gateway-ip>\n");
    printf("  serve config pack.<publisher>/<package>.network.dns     <dns-servers>\n");
    printf("  serve config pack.<publisher>/<package>.network.gateway --unset\n");
    printf("\n");
    printf("Options:\n");
    printf("  -h, --help\n");
    printf("      Print this help message\n");
    printf("  --unset\n");
    printf("      Remove the configuration key\n");
}

static int __build_pack_network_key(const char* option, char** keyOut)
{
    char*       tmpKey;
    const char* packIdBegin;
    const char* networkSep;
    const char* field;
    size_t      packIdLength;
    size_t      fieldLength;

    *keyOut = NULL;

    if (!strncmp(option, "pack-network.", strlen("pack-network."))) {
        const char* key = option + strlen("pack-network.");
        if (key[0] == 0) {
            fprintf(stderr, "serve: missing pack-network key\n");
            return -1;
        }
        *keyOut = strdup(key);
        return *keyOut != NULL ? 0 : -1;
    }

    if (strncmp(option, "pack.", strlen("pack.")) != 0) {
        fprintf(stderr, "serve: unsupported config option '%s'\n", option);
        return -1;
    }

    packIdBegin = option + strlen("pack.");
    networkSep = strstr(packIdBegin, ".network.");
    if (networkSep == NULL) {
        fprintf(stderr, "serve: invalid pack option '%s' (expected 'pack.<publisher>/<package>.network.<field>')\n", option);
        return -1;
    }

    packIdLength = (size_t)(networkSep - packIdBegin);
    if (packIdLength == 0) {
        fprintf(stderr, "serve: invalid pack option '%s' (missing pack id)\n", option);
        return -1;
    }

    field = networkSep + strlen(".network.");
    if (field[0] == 0) {
        fprintf(stderr, "serve: invalid pack option '%s' (missing network field)\n", option);
        return -1;
    }

    fieldLength = strlen(field);
    tmpKey = malloc(packIdLength + 1 + fieldLength + 1);
    if (tmpKey == NULL) {
        fprintf(stderr, "serve: out of memory\n");
        return -1;
    }

    memcpy(tmpKey, packIdBegin, packIdLength);
    tmpKey[packIdLength] = '.';
    memcpy(tmpKey + packIdLength + 1, field, fieldLength);
    tmpKey[packIdLength + 1 + fieldLength] = 0;

    *keyOut = tmpKey;
    return 0;
}

static int __handle_option(const char* option, const char* value, int unset)
{
    struct chef_config* config;
    void*               section;
    char*               key;
    int                 status;

    status = chef_dirs_initialize(CHEF_DIR_SCOPE_DAEMON);
    if (status != 0) {
        fprintf(stderr, "serve: failed to initialize directory code (%d)\n", status);
        return -1;
    }

    config = chef_config_load(chef_dirs_config());
    if (config == NULL) {
        fprintf(stderr, "serve: failed to load configuration: %s\n", strerror(errno));
        return -1;
    }

    section = chef_config_section(config, "pack-network");
    if (section == NULL) {
        fprintf(stderr, "serve: failed to load pack-network section from configuration: %s\n", strerror(errno));
        return -1;
    }

    if (__build_pack_network_key(option, &key) != 0) {
        return -1;
    }

    if (unset) {
        if (value != NULL) {
            fprintf(stderr, "serve: --unset cannot be combined with a value\n");
            free(key);
            return -1;
        }
        chef_config_set_string(config, section, key, NULL);
        chef_config_save(config);
        free(key);
        return 0;
    }

    if (value == NULL) {
        const char* current = chef_config_get_string(config, section, key);
        printf("%s = %s\n", option, current != NULL ? current : "(null)");
        free(key);
        return 0;
    }

    chef_config_set_string(config, section, key, value);
    chef_config_save(config);
    free(key);
    return 0;
}

int config_main(int argc, char** argv)
{
    const char* option = NULL;
    const char* value  = NULL;
    int         unset  = 0;

    if (argc > 2) {
        for (int i = 2; i < argc; i++) {
            if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
                __print_help();
                return 0;
            } else if (!strcmp(argv[i], "--unset")) {
                unset = 1;
            } else if (option == NULL) {
                option = argv[i];
            } else if (value == NULL) {
                value = argv[i];
            }
        }
    }

    if (option == NULL) {
        fprintf(stderr, "serve: missing option\n");
        __print_help();
        return -1;
    }

    return __handle_option(option, value, unset);
}
