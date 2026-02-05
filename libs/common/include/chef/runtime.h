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

#ifndef __CHEF_COMMON_RUNTIME_H__
#define __CHEF_COMMON_RUNTIME_H__

#include <chef/bits/runtime.h>

/**
 * @brief Parse a runtime string into a runtime info structure.
 */
extern struct chef_runtime_info* chef_runtime_info_parse(const char* name);
extern void                      chef_runtime_info_delete(struct chef_runtime_info* info);

/**
 * @brief Normalize a path according to the runtime's conventions.
 */
extern int chef_runtime_normalize_path(
    const char*                     path,
    const char*                     prefix,
    const struct chef_runtime_info* runtimeInfo,
    char**                          normalizedPathOut);

#endif // !__CHEF_COMMON_RUNTIME_H__
