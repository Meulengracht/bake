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

#ifndef __OVEN_PKGMNGRS_H__
#define __OVEN_PKGMNGRS_H__

// imports
struct ingredient;
struct list;

struct pkgmngr {
    int  (*make_available)(struct pkgmngr*, struct ingredient* ingredient);
    int  (*add_overrides)(struct pkgmngr*, struct list* environment);
    void (*destroy)(struct pkgmngr*);
};

extern struct pkgmngr* pkgmngr_pkgconfig_new(const char* root);

#endif //!__OVEN_PKGMNGRS_H__
