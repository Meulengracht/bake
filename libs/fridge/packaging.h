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

#ifndef __FRIDGE_PACKAGING_H__
#define __FRIDGE_PACKAGING_H__

#include <chef/package.h>
#include <chef/list.h>

struct packaging_import {
    struct list_item list_header;
    const char*      name;
};

struct packaging_params {
    const char*  prep_path;
    struct list* environment;
    struct list* imports; // list<packaging_import>
};

struct package_desc {
    const char*          path;
    const char*          publisher;
    const char*          package;
    struct chef_version* version;
};

extern int packaging_load(struct packaging_params* params);

extern int packaging_clear(const char* prep_path);

extern int packaging_make_available(const char* prep_path, struct package_desc*);

#endif //!__FRIDGE_PACKAGING_H__
