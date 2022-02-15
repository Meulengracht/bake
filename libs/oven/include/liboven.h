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

#ifndef __LIBOVEN_H__
#define __LIBOVEN_H__

struct oven_generate_options {
    const char* system;
    const char* arguments;
    char**      environment;
};

struct oven_build_options {
    const char* system;
    const char* arguments;
    char**      environment;
};

struct oven_pack_options {
    const char* compression;
};

/**
 * @brief 
 * 
 * @return int 
 */
extern int oven_initialize(void);

/**
 * @brief 
 * 
 * @param options 
 * @return int 
 */
extern int oven_configure(struct oven_generate_options* options);

/**
 * @brief 
 * 
 * @param options 
 * @return int 
 */
extern int oven_build(struct oven_build_options* options);

/**
 * @brief 
 * 
 * @param options 
 * @return int 
 */
extern int oven_pack(struct oven_pack_options* options);

/**
 * @brief 
 * 
 * @return int 
 */
extern int oven_cleanup(void);

#endif //!__LIBOVEN_H__
