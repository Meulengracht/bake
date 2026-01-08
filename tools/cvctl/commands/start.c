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

#include <chef/containerv/layers.h>
#include <chef/containerv.h>
#include <chef/platform.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vlog.h>

#include "commands.h"

static struct containerv_container* g_container = NULL;

static void __print_help(void)
{
    printf("Usage: cvctl start <root> [options]\n");
    printf("\n");
    printf("Options:\n");
    printf("  -h, --help\n");
    printf("      Print this help message\n");
}

static void __cleanup_systems(int sig)
{
    (void)sig;
    printf("termination requested, cleaning up\n"); // not safe
    if (g_container != NULL) {
        containerv_destroy(g_container);
    }
    vlog_cleanup();
    _Exit(0);
}

static struct containerv_layer_context* __build_layer_context(const char* id, const char* rootfs)
{
    struct containerv_layer_context* layerContext;
    int                              status;
    struct containerv_layer          baseLayer = { 0 };

    baseLayer.type   = CONTAINERV_LAYER_BASE_ROOTFS;
    baseLayer.source = (char*)rootfs;

    // Compose layers into final rootfs
    status = containerv_layers_compose(
        &baseLayer, 1, id, &layerContext
    );
    
    if (status != 0) {
        VLOG_ERROR("cvctl", "__build_layer_context: failed to compose layers\n");
        return NULL;
    }
    return layerContext;
}

int start_main(int argc, char** argv, char** envp, struct cvctl_command_options* options)
{
    struct containerv_layer_context* layerContext;
    const char*                      rootfs = NULL;
    struct containerv_options*       cvopts;
    char*                            abspath;
    int                              result;

    // catch CTRL-C
    signal(SIGINT, __cleanup_systems);

    // handle individual help command
    if (argc > 2) {
        for (int i = 2; i < argc; i++) {
            if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
                __print_help();
                return 0;
            } else if (argv[i][0] != '-') {
                rootfs = argv[i];
            }
        }
    }

    if (rootfs == NULL) {
        fprintf(stderr, "cvctl: no chroot was specified\n");
        __print_help();
        return -1;
    }

    abspath = platform_abspath(rootfs);
    if (abspath == NULL) {
        fprintf(stderr, "cvctl: path %s is invalid\n", rootfs);
        return -1;
    }

    // initialize the logging system
    vlog_initialize(VLOG_LEVEL_DEBUG);

    cvopts = containerv_options_new();
    if (cvopts == NULL) {
        fprintf(stderr, "cvctl: failed to allocate memory for container options\n");
        return -1;
    }

    layerContext = __build_layer_context("cvctl-container", abspath);
    if (layerContext == NULL) {
        result = -1;
        goto cleanup;
    }

    containerv_options_set_layers(cvopts, layerContext);

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)

#elif defined(__linux__) || defined(__unix__)
    containerv_options_set_caps(cvopts, CV_CAP_FILESYSTEM | CV_CAP_PROCESS_CONTROL | CV_CAP_IPC);
#endif

    result = containerv_create(NULL, cvopts, &g_container);
    if (result) {
        fprintf(stderr, "cvctl: failed to create container\n");
        result = -1;
        goto cleanup;
    }

    for (;;);

cleanup:
    containerv_layers_destroy(layerContext);
    containerv_options_delete(cvopts);
    vlog_cleanup();
    free(abspath);
    return result;
}
