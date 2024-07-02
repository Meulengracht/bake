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

#include <chef/kitchen.h>
#include <errno.h>

int kitchen_initialize(struct kitchen_init_options* options, struct kitchen* kitchen)
{
    errno = ENOTSUP;
    return -1;
}

int kitchen_setup(struct kitchen_setup_options* options, struct kitchen* kitchen)
{
    errno = ENOTSUP;
    return -1;
}

int kitchen_purge(struct kitchen_purge_options* options)
{
    errno = ENOTSUP;
    return -1;
}

int kitchen_recipe_make(struct kitchen* kitchen, struct recipe* recipe)
{
    errno = ENOTSUP;
    return -1;
}

int kitchen_recipe_pack(struct kitchen* kitchen, struct recipe* recipe)
{
    errno = ENOTSUP;
    return -1;
}

int kitchen_recipe_clean(struct kitchen* kitchen)
{
    errno = ENOTSUP;
    return -1;
}

int kitchen_recipe_purge(struct kitchen* kitchen, struct kitchen_recipe_purge_options* options)
{
    errno = ENOTSUP;
    return -1;
}
