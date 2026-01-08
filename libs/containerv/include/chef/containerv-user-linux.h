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

#ifndef __CONTAINERV_USER_H__
#define __CONTAINERV_USER_H__

#include <sys/types.h>

struct containerv_user {
    char* name;
    uid_t uid;
    gid_t gid;
};

extern struct containerv_user* containerv_user_real(void);
extern struct containerv_user* containerv_user_effective(void);
extern struct containerv_user* containerv_user_from(char* name, uid_t uid, gid_t gid);
extern void                    containerv_user_delete(struct containerv_user* user);

#endif //!__CONTAINERV_USER_H__
