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

#ifndef __KITCHEN_USER_H__
#define __KITCHEN_USER_H__

struct kitchen_user {
    char*        caller_name;
    unsigned int caller_uid;
    unsigned int caller_gid;

    char*        effective_name;
    unsigned int effective_uid;
    unsigned int effective_gid;
};

extern int  kitchen_user_new(struct kitchen_user* user);
extern int  kitchen_user_regain_privs(struct kitchen_user* user);
extern int  kitchen_user_drop_privs(struct kitchen_user* user);
extern void kitchen_user_delete(struct kitchen_user* user);

#endif //!__KITCHEN_USER_H__
