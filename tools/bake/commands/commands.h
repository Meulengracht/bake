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

#ifndef __BAKE_COMMANDS_H__
#define __BAKE_COMMANDS_H__

#include <chef/cli.h>
#include <chef/recipe.h>
#include <stdlib.h>

struct bake_command_options {
    struct recipe* recipe;
    const char*    recipe_path;
    const char*    platform;
    struct list    architectures;
    const char*    cwd;
};

#endif //!__BAKE_COMMANDS_H__
