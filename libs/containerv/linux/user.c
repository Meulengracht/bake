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
#define _GNU_SOURCE // needed for getresuid and friends

#include <chef/containerv-user-linux.h>
#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <vlog.h>

struct containerv_user* containerv_user_real(void)
{
    struct passwd* real;
    uid_t          ruid, euid, suid;

    if (getresuid(&ruid, &euid, &suid)) {
        VLOG_ERROR("containerv", "failed to retrieve user details: %s\n", strerror(errno));
        return NULL;
    }
    VLOG_DEBUG("containerv", "real: %u, effective: %u, saved: %u\n", ruid, euid, suid);

    real = getpwuid(ruid);
    if (real == NULL) {
        VLOG_ERROR("containerv", "failed to retrieve current user details: %s\n", strerror(errno));
        return NULL;
    }
    return containerv_user_from(real->pw_name, real->pw_uid, real->pw_gid);
}

struct containerv_user* containerv_user_effective(void)
{
    struct passwd* effective;
    uid_t          ruid, euid, suid;

    if (getresuid(&ruid, &euid, &suid)) {
        VLOG_ERROR("containerv", "failed to retrieve user details: %s\n", strerror(errno));
        return NULL;
    }
    VLOG_DEBUG("containerv", "real: %u, effective: %u, saved: %u\n", ruid, euid, suid);

    effective = getpwuid(euid);
    if (effective == NULL) {
        VLOG_ERROR("containerv", "failed to retrieve current user details: %s\n", strerror(errno));
        return NULL;
    }
    return containerv_user_from(effective->pw_name, effective->pw_uid, effective->pw_gid);
}

struct containerv_user* containerv_user_lookup(const char* name)
{
    struct passwd*          entry;
    struct containerv_user* user = NULL;
    FILE*                   database;
    int                     error;

    if (name == NULL || *name == '\0') {
        errno = EINVAL;
        return NULL;
    }

    database = fopen("/etc/passwd", "re");
    if (database == NULL) {
        error = errno;
        VLOG_ERROR("containerv", "cannot open image account database /etc/passwd for user '%s': %s\n", name, strerror(error));
        errno = error;
        return NULL;
    }

    errno = 0;
    while ((entry = fgetpwent(database)) != NULL) {
        if (strcmp(entry->pw_name, name) == 0) {
            user = containerv_user_from(entry->pw_name, entry->pw_uid, entry->pw_gid);
            break;
        }
    }
    
    error = errno;
    if (!user && !error) {
        error = ENOENT;
    }
    fclose(database);
    
    if (!user) {
        VLOG_ERROR("containerv", "cannot resolve user '%s' in image /etc/passwd: %s\n", name, strerror(error));
    }
    errno = error;
    return user;
}

struct containerv_user* containerv_user_from(char* name, uid_t uid, gid_t gid)
{
    struct containerv_user* user;
    
    user = calloc(1, sizeof(struct containerv_user));
    if (user == NULL) {
        return NULL;
    }
    
    user->name = strdup(name);
    if (user->name == NULL) { free(user); return NULL; }
    user->uid = uid;
    user->gid = gid;
    return user;
}

void containerv_user_delete(struct containerv_user* user)
{
    free(user->name);
    free(user);
}

struct containerv_group* containerv_group_lookup(const char* name)
{
    struct group* entry;
    struct containerv_group* group = NULL;
    FILE* database;
    int error;

    if (name == NULL || *name == '\0') { errno = EINVAL; return NULL; }
    database = fopen("/etc/group", "re");
    if (database == NULL) {
        error = errno;
        VLOG_ERROR("containerv", "cannot open image account database /etc/group for group '%s': %s\n", name, strerror(error));
        errno = error;
        return NULL;
    }
    errno = 0;
    while ((entry = fgetgrent(database)) != NULL) {
        if (strcmp(entry->gr_name, name) == 0) {
            group = containerv_group_from(entry->gr_name, entry->gr_gid);
            break;
        }
    }
    error = errno;
    if (!group && !error) error = ENOENT;
    fclose(database);
    if (!group) {
        VLOG_ERROR("containerv", "cannot resolve group '%s' in image /etc/group: %s\n", name, strerror(error));
    }
    errno = error;
    return group;
}

struct containerv_group* containerv_group_from(char* name, gid_t gid)
{
    struct containerv_group* group;
    
    group = calloc(1, sizeof(struct containerv_group));
    if (group == NULL) {
        return NULL;
    }
    
    group->name = strdup(name);
    if (group->name == NULL) { free(group); return NULL; }
    group->gid = gid;
    return group;
}

void containerv_group_delete(struct containerv_group* group)
{
    free(group->name);
    free(group);
}
