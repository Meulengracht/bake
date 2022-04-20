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
#include <libplatform.h>
#include <liboven.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern const char* __get_install_path(void);

static int __verify_commands(struct list* commands)
{
    struct list_item* item;
    struct platform_stat stats;

    if (commands->count == 0) {
        return 0;
    }

    list_foreach(commands, item) {
        struct oven_pack_command* command = (struct oven_pack_command*)item;
        char*                     path;
        
        if (command->path == NULL || strlen(command->path) == 0) {
            fprintf(stderr, "oven: command %s has no path\n", command->name);
            return -1;
        }

        // verify the command points to something correct
        path = strpathcombine(__get_install_path(), command->path);
        if (path == NULL) {
            fprintf(stderr, "oven: failed to combine command path\n");
            return -1;
        }

        if (platform_stat(path, &stats)) {
            fprintf(stderr, "oven: could not find command path %s\n", path);
            free(path);
            return -1;
        }
        free(path);
    }
    return 0;
}

int oven_resolve_commands(struct list* commands, struct list* resolves)
{
    int status;

    status = __verify_commands(commands);
    if (status) {
        fprintf(stderr, "oven: failed to verify commands\n");
        return -1;
    }

    return 0;
}
