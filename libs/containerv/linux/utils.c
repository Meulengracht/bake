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

#include <stdlib.h>
#include <unistd.h>
#include "utils.h"

int utils_detach_process(void)
{
    pid_t pid;

    pid = setsid();
    if (pid < 0) {
        return -1;
    }

    pid = fork ();
    if (pid < 0) {
        return -1;
    }

    if (pid != 0) {
        // skip any CRT cleanup here
        _exit (EXIT_SUCCESS);
    }
    return 0;
}
