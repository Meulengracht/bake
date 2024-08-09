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

#ifndef __ROOTFS_DEBOOTSTRAP_H__
#define __ROOTFS_DEBOOTSTRAP_H__

/**
 * @brief Initializes the given path as a new rootfs by using debootstrap as
 * the backend.
 * 
 * @param path The path to an existing folder where the rootfs will be created.
 * @return int 0 for success, non-zero on error. (See errno for details)
 */
extern int container_rootfs_setup_debootstrap(const char* path);

#endif //!__BUILDING_DEBOOTSTRAP_H__
