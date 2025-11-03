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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <windows.h>
#include <vlog.h>

/**
 * Windows container image/rootfs management
 * This provides support for Windows container images
 */

int containerv_rootfs_create_base_image(const char* path)
{
    // TODO: Implement Windows base image creation
    // - Download Windows Server Core or Nano Server base image
    // - Extract layers to the specified path
    VLOG_WARNING("containerv", "Windows base image creation not yet implemented\n");
    return -1;
}

int containerv_rootfs_import_image(const char* source, const char* destination)
{
    // TODO: Implement image import functionality
    VLOG_WARNING("containerv", "Windows image import not yet implemented\n");
    return -1;
}
