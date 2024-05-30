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

#ifndef __LIBPKGMGR_H__
#define __LIBPKGMGR_H__

// imports
struct ingredient;
struct list;

struct pkgmngr_options {
    // root is the path to the chroot directory containing the rootfs
    const char* root;
    const char* target_platform;
    const char* target_architecture;
};

struct pkgmngr {
    int  (*make_available)(struct pkgmngr*, struct ingredient* ingredient);
    int  (*add_overrides)(struct pkgmngr*, struct list* environment);
    void (*destroy)(struct pkgmngr*);
};

extern struct pkgmngr* pkgmngr_pkgconfig_new(struct pkgmngr_options* options);

#endif //!__LIBPKGMGR_H__
