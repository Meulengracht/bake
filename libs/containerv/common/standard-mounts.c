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

#include "standard-mounts.h"
#include <stddef.h>

static const char* const g_linux_mountpoints[] = {
    "/proc",
    "/sys",
    "/sys/fs/cgroup",
    "/dev",
    "/dev/pts",
    "/dev/shm",
    "/dev/mqueue",
    NULL,
};

const char* const* containerv_standard_linux_mountpoints(void)
{
    return g_linux_mountpoints;
}
