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

#ifndef __COOKD_SERVER_STORAGE_H__
#define __COOKD_SERVER_STORAGE_H__

/**
 * @brief 
 */
extern char* storage_build_new(const char* id, unsigned int mb);

/**
 * @brief
 */
extern void storage_build_delete(char* path);

#endif //!__COOKD_SERVER_STORAGE_H__
