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

#ifndef __LIBFRIDGE_H__
#define __LIBFRIDGE_H__

struct fridge_ingredient {
    const char* publisher;
    const char* name;
    const char* description;
    const char* version;
};

/**
 * @brief 
 * 
 * @return int 
 */
extern int fridge_initialize(void);

/**
 * @brief 
 * 
 * @return int 
 */
extern int fridge_cleanup(void);

/**
 * @brief Tells the fridge that we want to use a specific ingredient for our recipe. If
 * the ingredient doesn't exist, it will be fetched from chef.
 * 
 * @param[In] ingredient 
 * @return int  
 */
extern int fridge_use_ingredient(struct fridge_ingredient* ingredient);

#endif //!__LIBFRIDGE_H__
