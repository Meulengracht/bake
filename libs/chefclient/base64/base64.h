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

#ifndef __LIBCHEF_BASE64_H__
#define __LIBCHEF_BASE64_H__

#include <stddef.h>

/**
 * @brief 
 * 
 * @param data 
 * @param len 
 * @param lenOut 
 * @return unsigned* 
 */
extern unsigned char* base64_encode(const unsigned char* data, size_t len, size_t* lenOut);

#endif //!__LIBCHEF_BASE64_H__
