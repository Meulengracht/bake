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

#include <unistd.h>
#include <sys/types.h>

int platform_chsize(int fd, long size)
{
	return ftruncate(fd, (off_t)size);
}

#elif defined(_WIN32)

#include <io.h>

int platform_chsize(int fd, long size)
{
	return _chsize(fd, size);
}

#error "chsize: not implemented for this platform"
#endif