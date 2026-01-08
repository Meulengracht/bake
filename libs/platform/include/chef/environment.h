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

#ifndef __PLATFORM_ENVIRONMENT_H__
#define __PLATFORM_ENVIRONMENT_H__

#include <chef/list.h>

/**
 * @brief 
 * 
 * @param parent 
 * @param additonal 
 * @return char** 
 */
extern char** environment_create(const char* const* parent, struct list* additonal);

/**
 * @brief 
 */
extern int environment_append_keyv(char** envp, char* key, char** values, char* sep);

/**
 * @brief
 */
extern char* environment_flatten(const char* const* environment, size_t* lengthOut);

/**
 * @brief
 */
extern char** environment_unflatten(const char* text);

/**
 * @brief 
 * 
 * @param environment 
 */
extern void environment_destroy(char** environment);

#endif //!__PLATFORM_ENVIRONMENT_H__
