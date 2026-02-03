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

#ifndef __CONTAINERV_STD_MOUNTS_H__
#define __CONTAINERV_STD_MOUNTS_H__

// Internal shared list of standard Linux mountpoints used by OCI specs and rootfs prep.
// Paths are Linux-style absolute paths (e.g. "/proc"). The list is NULL-terminated.
const char* const* containerv_standard_linux_mountpoints(void);

#endif // !__CONTAINERV_STD_MOUNTS_H__
