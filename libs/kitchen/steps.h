/**
 * Copyright 2024, Philip Meulengracht
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

#ifndef __KITCHEN_STEPS_H__
#define __KITCHEN_STEPS_H__

// imports where we do not actually care about the .h
struct recipe;
struct oven_recipe_options;
struct recipe_part;

extern char* kitchen_toolchain_resolve(struct recipe* recipe, const char* toolchain, const char* platform);
extern void  oven_recipe_options_construct(struct oven_recipe_options* options, struct recipe_part* part, const char* toolchain);


#endif //!__KITCHEN_STEPS_H__
