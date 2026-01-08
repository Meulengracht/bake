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

#ifndef __BAKECTL_COMMANDS_H__
#define __BAKECTL_COMMANDS_H__

#include <chef/bake.h>
#include <liboven.h>

struct bakectl_command_options {
    const char* part;
    const char* step;
};

extern int __initialize_oven_options(struct oven_initialize_options* options, struct __bakelib_context* context);
extern void __destroy_oven_options(struct oven_initialize_options* options);

#endif //!__BAKECTL_COMMANDS_H__
