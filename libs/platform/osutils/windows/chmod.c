/**
 * Copyright 2024, Philip Meulengracht
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
#include <sys/types.h>
#include <sys/stat.h>
#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

int platform_chmod(const char* path, uint32_t permissions)
{
	if(_chmod(path, permissions) == -1)
	{
		switch (errno)
		{
			case EINVAL:
				fprintf(stderr, "Invalid parameter to chmod.\n");
				break;
			case ENOENT:
				fprintf(stderr, "File %s not found\n", path);
				break;
			default:
				fprintf(stderr, "Unexpected error in chmod.\n");
		}
	}
	else
	{
		if (permissions == _S_IREAD)
			printf("Mode set to read-only\n" );
		else if (permissions & _S_IWRITE)
			printf("Mode set to read/write\n" );
	}
	fflush(stderr);
}
