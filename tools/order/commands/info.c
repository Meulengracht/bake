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

#include <libchefclient.h>
#include <stdio.h>
#include <string.h>

static void __print_help(void)
{
    printf("Usage: bake info <pack-name>\n");
}

int info_main(int argc, char** argv)
{
    if (argc > 2) {
        for (int i = 2; i < argc; i++) {
            if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
                __print_help();
                return 0;
            }
        }
    }

    // initialize chefclient
    chefclient_initialize();

    // retrieve information about the pack

    // cleanup chefclient
    chefclient_cleanup();
    return -1;
}
