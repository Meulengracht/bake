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

#ifndef __CHEF_DIRS_H__
#define __CHEF_DIRS_H__

#include <stdio.h>

/**
 * @brief 
 * 
 * @return int 
 */
extern int chef_dirs_initialize(void);

/**
 * @brief Returns the path to the root of the chef data directory.
 */
extern const char* chef_dirs_root(void);

/**
 * @brief
 */
extern const char* chef_dirs_fridge(void);

/**
 * @brief
 */
extern const char* chef_dirs_store(void);

/**
 * @brief
 */
extern const char* chef_dirs_kitchen(const char* uuid);

/**
 * @brief
 */
extern int chef_dirs_ensure(const char* path);

/**
 * @brief 
 * 
 */
extern FILE* chef_dirs_contemporary_file(char** rpath);

#endif //!__CHEF_DIRS_H__
