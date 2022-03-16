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

int platform_filetype(const char* path, enum platform_filetype* typeOut)
{
	struct stat st;
	if (lstat(path, &st) != 0) {
		return -1;
	}

	switch (st.st_mode & S_IFMT) {
		case S_IFREG:
			*typeOut = PLATFORM_FILETYPE_FILE;
			break;
		case S_IFDIR:
			*typeOut = PLATFORM_FILETYPE_DIRECTORY;
			break;
		case S_IFLNK:
			*typeOut = PLATFORM_FILETYPE_SYMLINK;
			break;
		default:
			*typeOut = PLATFORM_FILETYPE_UNKNOWN;
			break;
	}
	return 0;
}

#else
#error "filetype: not implemented for this platform"
#endif
