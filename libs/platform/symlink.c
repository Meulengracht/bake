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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char* __prefix_path(const char* base, const char* path)
{
	// If 'path' is not an absolute path, then we assume it is relative to the
	// 'base' path, but the base path is a file path, so we need to remove the last
	// component
	if (path[0] != '/') {
		char* result = calloc(1, strlen(base) + strlen(path) + 2);
		char* last;
		if (result == NULL) {
			errno = ENOMEM;
			return NULL;
		}

		last = strrchr(base, '/');
		if (last == NULL) {
			strcpy(result, "/");
		} else {
			strncpy(result, base, (last - base) + 1);
			result[(last - base) + 1] = '\0';
		}
		strcat(result, path);
		return result;
	}
	return strdup(path);
}

#ifdef __linux__

#include <sys/stat.h>
#include <unistd.h>

int platform_symlink(const char* path, const char* target)
{
	char* targetFullPath;
	int   status;

	if (path == NULL || target == NULL) {
		errno = EINVAL;
		return -1;
	}

	targetFullPath = __prefix_path(path, target);
	if (targetFullPath == NULL) {
		return -1;
	}

	status = symlink(path, targetFullPath);
	free(targetFullPath);
	if (status) {
        // ignore it if it exists, in theory we would like to 'update it' if 
        // exists, but for now just ignore
        if (errno == EEXIST) {
            return 0;
        }
		return -1;
	}
	return 0;
}

#else
#error "symlink: not implemented for this platform"
#endif
