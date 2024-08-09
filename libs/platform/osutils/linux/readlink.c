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
#include <chef/platform.h>
#include <linux/limits.h>
#include <stdlib.h>
#include <sys/stat.h>

int platform_readlink(const char* path, char** bufferOut)
{
	char* buffer;

	if (path == NULL || bufferOut == NULL) {
		errno = EINVAL;
		return -1;
	}

	buffer = calloc(1, PATH_MAX);
	if (buffer == NULL) {
		errno = ENOMEM;
		return -1;
	}

	if (readlink(path, buffer, PATH_MAX - 1) == -1) {
		free(buffer);
		return -1;
	}

	*bufferOut = buffer;
	return 0;
}
