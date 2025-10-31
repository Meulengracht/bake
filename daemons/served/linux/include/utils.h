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

#ifndef __SERVED_UTILS_H__
#define __SERVED_UTILS_H__

typedef struct gracht_server gracht_server_t;
struct served_mount;


/**
 * @brief 
 */
extern char* served_paths_path(const char* path);

/**
 * @brief 
 * 
 * @param path 
 * @param mountPoint
 * @param mountOut 
 * @return int 
 */
extern int served_mount(const char* path, const char* mountPoint, struct served_mount** mountOut);

/**
 * @brief 
 * 
 * @param mount 
 */
extern void served_unmount(struct served_mount* mount);

/**
 * 
 * @return
 */
extern gracht_server_t* served_gracht_server(void);

/**
 * @brief Verifies a publisher against the database of proofs. 
 */
extern int utils_verify_publisher(const char* publisher);

/**
 * @brief Verifies the package and it's publisher against the database of proofs.
 */
extern int utils_verify_package(const char* publisher, const char* package, int revision);

#endif //!__SERVED_UTILS_H__
