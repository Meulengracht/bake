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
#define _GNU_SOURCE // needed for getresuid and friends

#include <chef/user.h>
#include <errno.h>
#include <pwd.h>
#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <vlog.h>

int containerv_user_new(struct containerv_user* user)
{
    uid_t ruid, euid, suid;
    struct passwd *effective;
    struct passwd *real;
    
    if (getresuid(&ruid, &euid, &suid)) {
        VLOG_ERROR("containerv", "failed to retrieve user details: %s\n", strerror(errno));
        return -1;
    }
    VLOG_DEBUG("containerv", "real: %u, effective: %u, saved: %u\n", ruid, euid, suid);

    real = getpwuid(ruid);
    if (real == NULL) {
        VLOG_ERROR("containerv", "failed to retrieve current user details: %s\n", strerror(errno));
        return -1;
    }

    // caller is the current actual user.
    user->caller_name = strdup(real->pw_name);
    user->caller_uid = real->pw_uid;
    user->caller_gid = real->pw_gid;

    effective = getpwuid(euid);
    if (effective == NULL) {
        VLOG_ERROR("containerv", "failed to retrieve executing user details: %s\n", strerror(errno));
        return -1;
    }

    user->effective_name = strdup(effective->pw_name);
    user->effective_uid = effective->pw_uid;
    user->effective_gid = effective->pw_gid;
    return 0;
}

void containerv_user_delete(struct containerv_user* user)
{
    free(user->caller_name);
    free(user->effective_name);
}
