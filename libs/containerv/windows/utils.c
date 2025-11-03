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

#include <windows.h>
#include <vlog.h>

/**
 * Windows container utilities
 */

int containerv_utils_create_directories(const char* path)
{
    // TODO: Implement directory creation for Windows
    VLOG_WARNING("containerv", "Windows directory creation not yet fully implemented\n");
    return 0;
}

int containerv_utils_mount_volume(const char* source, const char* target)
{
    // TODO: Implement volume mounting for Windows containers
    VLOG_WARNING("containerv", "Windows volume mounting not yet implemented\n");
    return 0;
}
