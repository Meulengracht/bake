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
 */

#include <errno.h>
#include <chef/platform.h>
#include <sys/stat.h>

int platform_stat(const char* path, struct platform_stat* stats)
{
	struct stat st;
	if (lstat(path, &st) != 0) {
		printf("lstat failed: %s\n", strerror(errno));
		printf("path was: %s\n", path);
		return -1;
	}

	stats->permissions = st.st_mode & 0777;
	stats->size        = st.st_size;
	switch (st.st_mode & S_IFMT) {
		case S_IFREG:
			stats->type = PLATFORM_FILETYPE_FILE;
			break;
		case S_IFDIR:
			stats->type = PLATFORM_FILETYPE_DIRECTORY;
			break;
		case S_IFLNK:
			stats->type = PLATFORM_FILETYPE_SYMLINK;
			break;
		default:
			stats->type = PLATFORM_FILETYPE_UNKNOWN;
			break;
	}
	return 0;
}
