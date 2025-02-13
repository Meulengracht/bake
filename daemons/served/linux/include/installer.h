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

#ifndef __SERVED_INSTALLER_H__
#define __SERVED_INSTALLER_H__

/**
 *
 * @param path
 * @return
 */
extern void served_installer_install(const char* publisher, const char* path);

/**
 *
 * @param package
 * @return
 */
extern void served_installer_uninstall(const char* package);

#endif //!__SERVED_INSTALLER_H__
