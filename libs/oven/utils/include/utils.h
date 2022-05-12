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

#ifndef __LIBOVEN_UTILS_H__
#define __LIBOVEN_UTILS_H__

/**
 * @brief 
 * 
 * @param parent 
 * @param additonal 
 * @return char** 
 */
extern char** oven_environment_create(const char* const* parent, struct list* additonal);

/**
 * @brief 
 * 
 * @param environment 
 */
extern void oven_environment_destroy(char** environment);

/**
 * @brief 
 * 
 * @param path 
 * @param checkpoint 
 * @return int 
 */
extern int oven_checkpoint_create(const char* path, const char* checkpoint);

/**
 * @brief 
 * 
 * @param path 
 * @param checkpoint 
 * @return int 
 */
extern int oven_checkpoint_remove(const char* path, const char* checkpoint);

/**
 * @brief 
 * 
 * @param path 
 * @param checkpoint 
 * @return int 
 */
extern int oven_checkpoint_contains(const char* path, const char* checkpoint);

#endif //!__LIBOVEN_UTILS_H__
