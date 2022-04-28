/**
 * Copyright 2022, Philip Meulengracht
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

#ifndef __SERVED_APPLICATION_H__
#define __SERVED_APPLICATION_H__

struct served_command {
    const char* name;
};

struct served_application {
    const char* name;
};

/**
 *
 * @param application
 * @return
 */
extern int served_application_mount(struct served_application* application);

/**
 *
 * @param application
 * @return
 */
extern int served_application_start_daemons(struct served_application* application);

#endif //!__SERVED_APPLICATION_H__
