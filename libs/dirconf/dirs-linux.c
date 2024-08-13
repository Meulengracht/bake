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

#include <chef/platform.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <vlog.h>

static struct {
    uid_t       real_user;
    const char* root;
    const char* fridge;
    const char* store;
    const char* kitchen;
} g_dirs = { NULL };

static int __directory_exists(
    const char* path)
{
    struct stat st;
    if (stat(path, &st)) {
        if (errno == ENOENT) {
            return 0;
        }
        return -1;
    }
    return S_ISDIR(st.st_mode) ? 1 : -1;
}

static int __mkdir_as(const char* path, unsigned int mode, uid_t uid, gid_t gid)
{
    char   ccpath[PATH_MAX];
    char*  p = NULL;
    size_t length;
    int    status;

    status = snprintf(ccpath, sizeof(ccpath), "%s", path);
    if (status >= sizeof(ccpath)) {
        errno = ENAMETOOLONG;
        return -1; 
    }

    length = strlen(ccpath);
    if (ccpath[length - 1] == '/') {
        ccpath[length - 1] = 0;
    }

    for (p = ccpath + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            
            status = __directory_exists(ccpath);
            if (status != 1) {
                status = mkdir(ccpath, mode);
                if (status) {
                    VLOG_ERROR("dirs", "failed to create path %s\n", ccpath);
                    return status;
                } else if (!status) {
                    // new directory, ensure correct permissions
                    status = chown(ccpath, uid, gid);
                    if (status) {
                        VLOG_ERROR("dirs", "failed to change ownership of %s\n", ccpath);
                        return status;
                    }
                }
            } else if (status < 0) {
                VLOG_ERROR("dirs", "failed to stat %s\n", ccpath);
                return status;
            }

            *p = '/';
        }
    }
    return mkdir(ccpath, mode);
}

static uid_t __real_user(void)
{
    uid_t euid, suid, ruid;
    getresuid(&ruid, &euid, &suid);
    return ruid;
}

static int __ensure_chef_dirs(void)
{
    struct {
        const char** path;
    } paths[] = {
        { &g_dirs.root },
        { &g_dirs.fridge },
        { &g_dirs.store },
        { &g_dirs.kitchen },
        { NULL },
    };
    for (int i = 0; paths[i].path != NULL; i++) {
        int status = __mkdir_as(*paths[i].path, 0755, g_dirs.real_user, g_dirs.real_user);
        if (status) {
            VLOG_ERROR("dirs", "failed to create %s\n", *paths[i].path);
            return -1;
        }
    }
    return 0;
}

int chef_dirs_initialize(void)
{
    int   status;
    char  buffer[PATH_MAX] = { 0 };
    uid_t realUser = __real_user();

    // if running as root, then error as we do not want this
    if (realUser == 0) {
        VLOG_ERROR("dirs", "DETECTED running as root, this is not recommended and directories will not be created for root\n");
        return -1;
    }

    status = platform_getuserdir(&buffer[0], sizeof(buffer) - 1);
    if (status) {
        VLOG_ERROR("dirs", "failed to resolve user directory\n");
        return -1;
    }

    g_dirs.real_user = realUser;
    g_dirs.root = strpathcombine(&buffer[0], ".chef");
    g_dirs.fridge = strpathcombine(g_dirs.root, "fridge");
    g_dirs.store = strpathcombine(g_dirs.root, "store");
    g_dirs.kitchen = strpathcombine(g_dirs.root, "kitchen");
    if (g_dirs.root == NULL || g_dirs.fridge == NULL ||
        g_dirs.store == NULL || g_dirs.kitchen == NULL) {
        VLOG_ERROR("dirs", "failed to allocate memory for paths\n");
        return -1;
    }
    return __ensure_chef_dirs();
}

const char* chef_dirs_root(void)
{
    return g_dirs.root;
}

const char* chef_dirs_fridge(void)
{
    return g_dirs.fridge;
}

const char* chef_dirs_store(void)
{
    return g_dirs.store;
}

const char* chef_dirs_kitchen(const char* uuid)
{
    if (uuid != NULL) {
        return strpathcombine(g_dirs.kitchen, uuid);
    }
    return g_dirs.kitchen;
}

const char* chef_dirs_ensure(const char* path)
{
    return __mkdir_as(path, 0755, g_dirs.real_user, g_dirs.real_user);
}
