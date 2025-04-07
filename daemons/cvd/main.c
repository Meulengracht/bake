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

#include "chef-config.h"
#include <chef/dirs.h>
#include <server.h>
#include "chef_cvd_service_server.h"
#include <vlog.h>

// the server object
static gracht_server_t* g_server = NULL;

static void __print_help(void)
{
    printf("Usage: cvd [options]\n\n");
    printf("Container daemon for the chef build system. This manages active containers\n");
    printf("and are used by both the remote builder (cook) and the local builder (bake).\n");
    printf("\n");
    printf("Options:\n");
    printf("  -v\n");
    printf("      Provide this for improved logging output\n");
    printf("  --version\n");
    printf("      Print the version of cvd\n");
    printf("  -h, --help\n");
    printf("      Print this help message\n");
}

int main(int argc, char** argv)
{
    struct gracht_server_configuration config;
    int                                status;
    int                                logLevel = VLOG_LEVEL_TRACE;
    FILE*                              debuglog;
    char*                              debuglogPath;

    // parse options
    if (argc > 1) {
        for (int i = 1; i < argc; i++) {
            if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
                __print_help();
                return 0;
            } else if (!strcmp(argv[i], "--version")) {
                printf("cvd: version " PROJECT_VER "\n");
                return 0;
            } else if (!strncmp(argv[i], "-v", 2)) {
                int li = 1;
                while (argv[i][li++] == 'v') {
                    logLevel++;
                }
            }
        }
    }

    // initialize logging
    vlog_initialize((enum vlog_level)logLevel);
    atexit(vlog_cleanup);

    // initialize directories
    status = chef_dirs_initialize();
    if (status) {
        fprintf(stderr, "cvd: failed to initialize directories\n");
        return -1;
    }

    // load config
    status = cvd_config_load(chef_dirs_config());
    if (status) {
        fprintf(stderr, "cvd: failed to load configuation\n");
        return -1;
    }

    // add log file to vlog
    debuglog = chef_dirs_contemporary_file("cvd", "log", &debuglogPath);
    if (debuglog == NULL) {
        fprintf(stderr, "cvd: failed to open log file\n");
        return -1;
    }
    vlog_add_output(debuglog, 1);
    vlog_set_output_level(debuglog, VLOG_LEVEL_DEBUG);

    printf("log opened at %s\n", debuglogPath);
    free(debuglogPath);

    // initialize the server configuration
    gracht_server_configuration_init(&config);

    // start up the server
    status = cvd_initialize_server(&config, &g_server);

    // we register protocols
    gracht_server_register_protocol(g_server, &chef_cvd_server_protocol);

    // use the default server loop
    return gracht_server_main_loop(g_server);
}
