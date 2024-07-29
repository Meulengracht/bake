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

static int __change_user(unsigned int uid, unsigned int gid)
{
    int status;

    status = setgid(uid);
    if (status) {
        VLOG_ERROR("containerv", "failed setgid: %s\n", strerror(errno));
        return status;
    }
    status = setuid(gid);
    if (status) {
        VLOG_ERROR("containerv", "failed setuid: %s\n", strerror(errno));
        return status;
    }
    return 0;
}

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

    // caller should not be root
    if (real->pw_uid == 0) {
        VLOG_WARNING("containerv", "INVOKED AS SUDO, PLEASE BE CAREFUL\n");
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

    // effective should be set to root
    if (effective->pw_uid != 0 && effective->pw_gid != 0) {
        VLOG_ERROR("containerv", "bake must run under the root account/group\n");
        return -1;
    }

    user->effective_name = strdup(effective->pw_name);
    user->effective_uid = effective->pw_uid;
    user->effective_gid = effective->pw_gid;
    return 0;
}

static int __chef_user_switch(unsigned int ruid, unsigned int rgid, unsigned int euid, unsigned int egid)
{
    int status;
    VLOG_DEBUG("containerv", "__chef_user_switch(to=%u/%u, from=%u/%u)\n", ruid, rgid, euid, egid);

    status = setreuid(euid, ruid);
    if (status) {
        VLOG_ERROR("containerv", "failed setreuid: %s\n", strerror(errno));
        return status;
    }
    status = setregid(egid, rgid);
    if (status) {
        VLOG_ERROR("containerv", "failed setregid: %s\n", strerror(errno));
        return status;
    }
    return 0;
}

int containerv_user_regain_privs(struct containerv_user* user)
{
    VLOG_DEBUG("containerv", "containerv_user_regain_privs()\n");
    return __chef_user_switch(
        user->effective_uid,
        user->effective_gid,
        user->caller_uid,
        user->caller_gid
    );
}

int containerv_user_drop_privs(struct containerv_user* user)
{
    VLOG_DEBUG("containerv", "containerv_user_drop_privs()\n");
    return __chef_user_switch(
        user->caller_uid,
        user->caller_gid,
        user->effective_uid,
        user->effective_gid
    );
}

void containerv_user_delete(struct containerv_user* user)
{
    free(user->caller_name);
    free(user->effective_name);
}
