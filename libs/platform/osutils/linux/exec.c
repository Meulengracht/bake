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

#include <chef/platform.h>
#include <stdio.h>
#include <stdlib.h>

char* platform_exec(const char* cmd)
{
    FILE*  stream;
    int    status;
    char*  result;
    size_t read;

    result = malloc(4096);
    if (result == NULL) {
        return NULL;
    }

    stream = popen(cmd, "r");
    if (stream == NULL) {
        return NULL;
    }

    read = fread(result, 1, 4096, stream);
    if (read == 0) {
        (void)pclose(stream);
        free(result);
        return NULL;
    }

    status = pclose(stream);
    if (status) {
        // log it?
    }
    return result;
}
