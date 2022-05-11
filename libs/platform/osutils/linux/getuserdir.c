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
#include <pwd.h>
#include <string.h>
#include <unistd.h>

int platform_getuserdir(char* buffer, size_t length)
{
    struct passwd* pw;
    
    pw = getpwuid(getuid());
    if (pw == NULL) {
        return -1;
    }

    strncpy(buffer, pw->pw_dir, length);
    return 0;
}
