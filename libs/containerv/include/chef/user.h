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

#ifndef __CONTAINERV_USER_H__
#define __CONTAINERV_USER_H__

struct containerv_user {
    char*        caller_name;
    unsigned int caller_uid;
    unsigned int caller_gid;

    char*        effective_name;
    unsigned int effective_uid;
    unsigned int effective_gid;
};

extern struct containerv_user* containerv_user_new(void);
extern void                    containerv_user_delete(struct containerv_user* user);

#endif //!__CONTAINERV_USER_H__
