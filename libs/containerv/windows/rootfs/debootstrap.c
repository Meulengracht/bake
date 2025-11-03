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

#include <chef/rootfs/debootstrap.h>
#include <vlog.h>

// Windows placeholder for debootstrap functionality
// On Windows, we would use WSL or container base images instead

int containerv_rootfs_debootstrap(
    const char*                            path,
    struct containerv_rootfs_debootstrap*  options
)
{
    VLOG_ERROR("containerv", "debootstrap is not supported on Windows\n");
    VLOG_ERROR("containerv", "Use WSL base images or container images instead\n");
    return -1;
}
