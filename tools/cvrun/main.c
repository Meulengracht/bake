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

#include <chef/containerv.h>
#include <chef/platform.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "chef-config.h"
#include <vlog.h>

static struct containerv_container* g_container = NULL;

static void __print_help(void)
{
    printf("Usage: cvrun <root> [options]\n");
    printf("\n");
    printf("Options:\n");
    printf("  -h, --help\n");
    printf("      Print this help message\n");
    printf("  -v, --version\n");
    printf("      Print the version of order\n");
}

static void __cleanup_systems(int sig)
{
    (void)sig;
    printf("termination requested, cleaning up\n"); // not safe
    if (g_container != NULL) {
        container_destroy(g_container);
    }
    vlog_cleanup();
    _Exit(0);
}

int main(int argc, char** argv, char** envp)
{
    const char* rootfs = NULL;
    char*       abspath;
    int         result;

    // catch CTRL-C
    signal(SIGINT, __cleanup_systems);

    // first argument must be the command if not --help or --version
    if (argc > 1) {
        if (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")) {
            __print_help();
            return 0;
        }

        if (!strcmp(argv[1], "-v") || !strcmp(argv[1], "--version")) {
            printf("cvrun: version " PROJECT_VER "\n");
            return 0;
        }

        rootfs = argv[1];
    }

    if (rootfs == NULL) {
        fprintf(stderr, "cvrun: no chroot was specified\n");
        __print_help();
        return -1;
    }

    abspath = platform_abspath(rootfs);
    if (abspath == NULL) {
        fprintf(stderr, "cvrun: path %s is invalid\n", rootfs);
        return -1;
    }

    // initialize the logging system
    vlog_initialize();
    vlog_set_level(VLOG_LEVEL_DEBUG);
    vlog_add_output(stdout);

    result = containerv_create(
        abspath,
        CV_CAP_FILESYSTEM | CV_CAP_PROCESS_CONTROL,
        NULL, 0,
        &g_container
    );
    if (result) {
        fprintf(stderr, "cvrun: failed to create container\n");
        vlog_cleanup();
        free(abspath);
        return -1;
    }

    for (;;);

    vlog_cleanup();
    free(abspath);
    return 0;
}
