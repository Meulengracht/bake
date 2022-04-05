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
#include <stdlib.h>
#include <string.h>

void strbasename(const char* path, char* buffer, size_t bufferSize)
{
	char* start;
    int   i;

    if (path == NULL || buffer == NULL || bufferSize == 0) {
        errno = EINVAL;
        return;
    }
	
	start = strrchr(path, '/');
	if (start == NULL) {
		start = (char*)path;
	} else {
		start++;
	}

    for (i = 0; start[i] && start[i] != '.' && i < (int)(bufferSize - 1); i++) {
        buffer[i] = start[i];
    }
    buffer[i] = '\0';
}