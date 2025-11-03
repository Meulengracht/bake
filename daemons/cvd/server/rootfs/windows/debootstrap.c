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
#include <string.h>
#include <vlog.h>

int cvd_rootfs_setup_debootstrap(const char* path)
{
    VLOG_DEBUG("cvd", "cvd_rootfs_setup_debootstrap(path=%s) - Windows\n", path);
    
    // Debootstrap is a Linux-specific tool for setting up Debian-based rootfs
    // On Windows, we would use different approaches:
    // 1. For Windows containers: Use Windows Server Core or Nano Server base images
    // 2. For Linux containers on Windows: Use WSL2 distributions
    
    VLOG_ERROR("cvd", "Debootstrap is not supported on Windows\n");
    VLOG_ERROR("cvd", "For Linux containers on Windows, use WSL2 or pre-built container images\n");
    VLOG_ERROR("cvd", "For Windows containers, use Windows base images\n");
    
    return -1;
}
