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

#ifndef __SERVED_STATE_H__
#define __SERVED_STATE_H__

struct served_application;

/**
 * @brief
 *
 * @return
 */
extern int served_state_load(void);

/**
 * @brief
 *
 * @return
 */
extern int served_state_save(void);

/**
 * @brief
 *
 * @param applicationsOut
 * @param applicationsCount
 * @return
 */
extern int served_state_get_applications(struct served_application*** applicationsOut, int* applicationsCount);

/**
 * @brief 
 * 
 * @param application 
 * @return int 
 */
extern int served_state_add_application(struct served_application* application);

/**
 * @brief 
 * 
 * @param application 
 * @return int 
 */
extern int served_state_remove_application(struct served_application* application);

#endif //!__SERVED_STATE_H__
