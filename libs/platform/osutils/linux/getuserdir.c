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

#include <chef/platform.h>
#include <pwd.h>
#include <string.h>

#ifdef CHEF_AS_SNAP
#include <stdlib.h>
static unsigned int __get_snap_uid(void)
{
    char* uidstr = getenv("SNAP_UID");
    if (uidstr == NULL) {
        // fallback
        return getuid();
    }
    return (unsigned int)atoi(uidstr); 
}
#endif

int platform_getuserdir(char* buffer, size_t length)
{
    struct passwd* pw;

#ifdef CHEF_AS_SNAP
    pw = getpwuid(__get_snap_uid());
#else
    pw = getpwuid(getuid());
#endif
    if (pw == NULL) {
        return -1;
    }

    strncpy(buffer, pw->pw_dir, length);
    return 0;
}
