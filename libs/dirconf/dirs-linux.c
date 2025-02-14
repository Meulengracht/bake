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

#define _GNU_SOURCE

#include <chef/platform.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <vlog.h>

static struct {
    uid_t       real_user;

    // per-user
    const char* root;
    const char* fridge;
    const char* store;
    const char* kitchen;

    // global
    const char* config;
} g_dirs = { 0, NULL, NULL, NULL, NULL };

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

static int __mkdir_if_not_exists(const char* path, unsigned int mode, uid_t uid, gid_t gid)
{
    int status = __directory_exists(path);
    if (status == 1) {
        return 0;
    } else if (status < 0) {
        VLOG_ERROR("dirs", "failed to stat %s\n", path);
        return status;
    }
    
    status = mkdir(path, mode);
    if (status) {
        VLOG_ERROR("dirs", "failed to create path %s\n", path);
        return status;
    }

    status = chown(path, uid, gid);
    if (status) {
        VLOG_ERROR("dirs", "failed to change ownership of %s\n", path);
    }
    return status;
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
            
            status = __mkdir_if_not_exists(ccpath, mode, uid, gid);
            if (status) {
                return status;
            }

            *p = '/';
        }
    }
    return __mkdir_if_not_exists(ccpath, mode, uid, gid);
}

static uid_t __real_user(void)
{
    uid_t euid, suid, ruid;
    getresuid(&ruid, &euid, &suid);
    return ruid;
}

static int __ensure_chef_user_dirs(void)
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
        int         status;
        const char* path = *paths[i].path;
        
        if (path == NULL) {
            continue;
        }

        status = __mkdir_as(path, 0755, g_dirs.real_user, g_dirs.real_user);
        if (status) {
            VLOG_ERROR("dirs", "failed to create %s\n", path);
            return -1;
        }
    }
    return 0;
}

static int __ensure_chef_global_dirs(void)
{
    struct {
        const char** path;
    } paths[] = {
        { &g_dirs.config },
        { NULL },
    };
    for (int i = 0; paths[i].path != NULL; i++) {
        int         status;
        const char* path = *paths[i].path;
        
        if (path == NULL) {
            continue;
        }

        status = __mkdir_as(path, 0644, 0, 0);
        if (status) {
            VLOG_ERROR("dirs", "failed to create %s\n", path);
            return -1;
        }
    }
    return 0;
}

static const char* __root_common_directory(void)
{
#ifdef CHEF_AS_SNAP
    // /var/snap/<snap>/common
    char* val = getenv("SNAP_COMMON");
    if (val != NULL) {
        return val;
    }
#endif
    return "/etc/chef";
}

static char* __root_common_user_directory(void)
{
    int   status;
    char  buffer[PATH_MAX] = { 0 };
#ifdef CHEF_AS_SNAP
    // /home/<user>/snap/<snap>/common
    char* val = getenv("SNAP_USER_COMMON");
    if (val != NULL) {
        return platform_strdup(val);
    }
#endif
    status = platform_getuserdir(&buffer[0], sizeof(buffer) - 1);
    if (status) {
        return NULL;
    }
    return strpathcombine(&buffer[0], ".chef");
}

static int __setup_root(void)
{
    VLOG_DEBUG("dirs", "DETECTED running as root, only chef_dirs_config() will be valid\n");

    // root directories are more or less hardwired
    // to shared system locations. This is almost solely used for the
    // daemon services. All other tools should run in some sort of user
    // context, and must protect themself against root invocations.
    g_dirs.config = __root_common_directory();

    return __ensure_chef_global_dirs();
}

int chef_dirs_initialize(void)
{
    uid_t realUser = __real_user();

    if (realUser == 0) {
        return __setup_root();
    }

    g_dirs.real_user = realUser;
    
    g_dirs.root = __root_common_user_directory();
    if (g_dirs.root == NULL) {
        VLOG_ERROR("dirs", "failed to resolve user directory\n");
        return -1;
    }

    g_dirs.fridge = strpathcombine(g_dirs.root, "fridge");
    g_dirs.store = strpathcombine(g_dirs.root, "store");
    g_dirs.kitchen = strpathcombine(g_dirs.root, "kitchen");
    if (g_dirs.root == NULL || g_dirs.fridge == NULL ||
        g_dirs.store == NULL || g_dirs.kitchen == NULL) {
        VLOG_ERROR("dirs", "failed to allocate memory for paths\n");
        return -1;
    }

    g_dirs.config = __root_common_directory();

    return __ensure_chef_user_dirs();
}

const char* chef_dirs_root(void)
{
    if (g_dirs.root == NULL) {
        VLOG_ERROR("dirs", "directories are NOT initialized!\n");
        return NULL;
    }
    return g_dirs.root;
}

const char* chef_dirs_fridge(void)
{
    if (g_dirs.fridge == NULL) {
        VLOG_ERROR("dirs", "chef_dirs_fridge() is not available\n");
        return NULL;
    }
    return g_dirs.fridge;
}

const char* chef_dirs_store(void)
{
    if (g_dirs.store == NULL) {
        VLOG_ERROR("dirs", "chef_dirs_store() is not available\n");
        return NULL;
    }
    return g_dirs.store;
}

const char* chef_dirs_kitchen(const char* uuid)
{
    if (g_dirs.kitchen == NULL) {
        VLOG_ERROR("dirs", "chef_dirs_kitchen() is not available\n");
        return NULL;
    }
    if (uuid != NULL) {
        return strpathcombine(g_dirs.kitchen, uuid);
    }
    return g_dirs.kitchen;
}

const char* chef_dirs_config(void)
{
    if (g_dirs.config == NULL) {
        VLOG_ERROR("dirs", "chef_dirs_config() is not available\n");
        return NULL;
    }
    return g_dirs.config;
}

int chef_dirs_ensure(const char* path)
{
    if (g_dirs.root == NULL) {
        VLOG_ERROR("dirs", "directories are NOT initialized!\n");
        return -1;
    }
    return __mkdir_as(path, 0755, g_dirs.real_user, g_dirs.real_user);
}

FILE* chef_dirs_contemporary_file(const char* name, const char* ext, char** rpath)
{
    char  buffer[128];
    FILE* stream;
    char* path;

    snprintf(&buffer[0], sizeof(buffer), "/tmp/%s-XXXXXX.%s", name, ext);
    if (mkstemps(&buffer[0], strlen(ext) + 1) < 0) {
        VLOG_ERROR("dirs", "failed to get a temporary filename for log: %i\n", errno);
        return NULL;
    }
    
    path = platform_strdup(&buffer[0]);
    if (path == NULL) {
        return NULL;
    }

    stream = fopen(path, "w+");
    if (stream == NULL) {
        VLOG_ERROR("dirs", "failed to open path %s for writing\n", path);
        free(path);
        return NULL;
    }

    if (fchmod(fileno(stream), 0644)) {
        VLOG_ERROR("dirs", "failed to open change mode of %s\n", path);
        free(path);
        return NULL;
    }

    if (rpath != NULL) {
        *rpath = path;
    } else {
        free(path);
    }
    return stream;
}
