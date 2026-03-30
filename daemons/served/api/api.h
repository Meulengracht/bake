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

#ifndef __SERVED_API_INTERNAL_H__
#define __SERVED_API_INTERNAL_H__

struct state_application;
struct chef_served_package;

extern unsigned int served_api_create_install_transaction(const char* packageName, const char* channel, int revision);
extern void         served_api_convert_app_to_info(struct state_application* application, struct chef_served_package* info);
extern void         served_api_cleanup_info(struct chef_served_package* info);

#endif /* __SERVED_API_INTERNAL_H__ */
