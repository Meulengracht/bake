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

struct served_install_store_options {
    const char* channel;
    int         revision;
};

/**
 *
 * @param path
 * @return
 */
extern void served_installer_install_local(const char* pack, const char* proof);
extern void served_installer_install_store(const char* package, struct served_install_store_options* options);

extern void served_installer_update();

extern void served_installer_switch();

/**
 *
 * @param package
 * @return
 */
extern void served_installer_uninstall(const char* package);

#endif //!__SERVED_INSTALLER_H__
