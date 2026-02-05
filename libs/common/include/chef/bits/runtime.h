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

#ifndef __CHEF_COMMON_RUNTIME_BITS_H__
#define __CHEF_COMMON_RUNTIME_BITS_H__

/**
 * @brief The required platform for the runtime of a package. This is primarily
 * used for identifying the required container type to run the package in.
 */
enum chef_target_runtime {
    CHEF_RUNTIME_UNSUPPORTED = 0,
    CHEF_RUNTIME_LINUX,
    CHEF_RUNTIME_WINDOWS,
};

struct chef_runtime_info {
    enum chef_target_runtime runtime;
    const char*              name;
    const char*              version;
};

#endif // !__CHEF_COMMON_RUNTIME_BITS_H__
