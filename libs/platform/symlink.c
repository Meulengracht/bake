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

#include <libplatform.h>

#ifdef __linux__

#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>

int platform_symlink(const char* path, const char* target)
{
	int status;
	
	if (path == NULL || target == NULL) {
		errno = EINVAL;
		return -1;
	}

	status = symlink(target, path);
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
