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

#ifndef __SERVED_APPLICATION_H__
#define __SERVED_APPLICATION_H__

struct served_mount;
struct containerv_container;

struct served_command {
    const char* name;
    const char* path;
    const char* arguments;
    int         type;

    // these are runtime state variables
    // and are not serialized to disk.
    const char* symlink;
    const char* data;
};

struct served_application {
    const char* name; // publisher/package
    const char* publisher;
    const char* package;
    int         major;
    int         minor;
    int         patch;
    int         revision;

    struct served_command* commands;
    int                    commands_count;

    // these are runtime state variables
    // and are not serialized to disk.
    struct served_mount*         mount;
    struct containerv_container* container;
};

/**
 * @brief
 *
 * @return
 */
extern struct served_application* served_application_new(void);

/**
 * @brief
 *
 * @param application
 */
extern void served_application_delete(struct served_application* application);

/**
 *
 * @param application
 * @return
 */
extern int served_application_load(struct served_application* application);

/**
 * 
 * @param application
 * @return
 */
extern int served_application_unload(struct served_application* application);

/**
 * @brief 
 * 
 * @param application 
 * @return int 
 */
extern int served_application_ensure_paths(struct served_application* application);

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
extern int served_application_unmount(struct served_application* application);

/**
 *
 * @param application
 * @return
 */
extern int served_application_start_daemons(struct served_application* application);

/**
 *
 * @param application
 * @return
 */
extern int served_application_stop_daemons(struct served_application* application);

#endif //!__SERVED_APPLICATION_H__
