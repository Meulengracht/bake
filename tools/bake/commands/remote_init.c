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
#include <chef/remote.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chef-config.h"
#include "commands.h"

static void __print_help(void)
{
    printf("Usage: bake remote init [options]\n");
    printf("  Remote can be used to execute recipes remotely for a configured\n");
    printf("  build-server. It will connect to the configured waiterd instance in\n");
    printf("  the configuration file (bake.json)\n");
    printf("  If the connection is severed between the bake instance and the waiterd\n");
    printf("  instance, the build can be resumed from the bake instance by invoking\n");
    printf("  'bake remote resume <ID>'\n\n");
    printf("Options:\n");
    printf("  -l,  --local\n");
    printf("      Configures the default local connections for waiterd,\n");
    printf("      this will only work if waiterd runs on the same machine\n");
    printf("      with the same default setup\n");
    printf("  -h,  --help\n");
    printf("      Shows this help message\n");
}

int remote_init_main(int argc, char** argv, char** envp, struct bake_command_options* options)
{
    int local = 0;
    int i;

    // handle arguments specifically for init
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "init")) {
            i++;
            break;
        }
    }

    if (i < argc) {
        if (!strcmp(argv[i], "-l") || !strcmp(argv[i], "--local")) {
            local = 1;
        } else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            __print_help();
            return 0;
        } else if (!strcmp(argv[i], "--version")) {
            printf("bake: version " PROJECT_VER "\n");
            return 0;
        }
    }

    if (local) {
        return remote_local_init_default();
    }
    return remote_wizard_init();
}
