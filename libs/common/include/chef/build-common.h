/**
 * Copyright 2023, Philip Meulengracht
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

#ifndef __CHEF_COMMON_H__
#define __CHEF_COMMON_H__

#include <chef/list.h>

struct chef_backend_make_options {
    int in_tree;
    int parallel;
};

struct meson_wrap_item {
    struct list_item list_header;
    const char*      name;
    const char*      ingredient;
};

struct chef_backend_meson_options {
    const char* cross_file;
    struct list wraps; // list<meson_wrap_item>
};

union chef_backend_options {
    struct chef_backend_make_options  make;
    struct chef_backend_meson_options meson;
};


#endif //!__CHEF_COMMON_H__
