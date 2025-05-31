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

#include <chef/dirs.h>
#include <chef/platform.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <vlog.h>

static struct {
    enum chef_dir_scope scope;
    uid_t               real_user;

    // per-user
    const char* root;
    const char* fridge;
    const char* store;
    const char* kitchen;

    // global
    const char* config;
} g_dirs = { 0, 0, NULL, NULL, NULL, NULL, NULL };

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
        unsigned int umode;
        // mode to use when the root is creating directories
        // which happens when run in daemon mode
        unsigned int rmode;
    } paths[] = {
        { &g_dirs.root, 0755, 0777 },
        { &g_dirs.fridge, 0755, 0777 },
        { &g_dirs.store, 0755, 0777 },
        { &g_dirs.kitchen, 0755, 0777 },
        { NULL },
    };
    for (int i = 0; paths[i].path != NULL; i++) {
        int          status;
        const char*  path = *paths[i].path;
        unsigned int mode = paths[i].umode;
        
        if (path == NULL) {
            continue;
        }

        if (g_dirs.real_user == 0) {
            mode = paths[i].rmode;
        }
        
        status = __mkdir_as(path, paths[i].rmode, g_dirs.real_user, g_dirs.real_user);
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
        unsigned int mode;
    } paths[] = {
        // Create the root directory (which is the workspace) with
        // relaxed permissions to allow for non-root tools to work
        // with the filesystem
        { &g_dirs.root,   0777 },
        // Config can be more restrictive, we do not want arbitrary access
        // here
        { &g_dirs.config, 0644 },
        { NULL },
    };
    for (int i = 0; paths[i].path != NULL; i++) {
        int         status;
        const char* path = *paths[i].path;
        
        if (path == NULL) {
            continue;
        }

        status = __mkdir_as(path, paths[i].mode, 0, 0);
        if (status) {
            VLOG_ERROR("dirs", "failed to create %s\n", path);
            return -1;
        }
    }
    return 0;
}

static char* __strdup_fail(const char* str) {
    char* copy = strdup(str);
    if (copy == NULL) {
        VLOG_FATAL("dirs", "failed to allocate memory for %s\n", str);
    }
    return copy;
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

static char* __common_user_directory(void)
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

static int __initialize_daemon(void)
{
    if (g_dirs.real_user != 0) {
        VLOG_ERROR("dirs", "running daemons as non-root user is not currently supported\n");
        return -1;
    }

    g_dirs.root    = __strdup_fail("/tmp/chef");
    g_dirs.config  = __strdup_fail(__root_common_directory());
    g_dirs.fridge  = strpathcombine(g_dirs.config, "fridge");
    g_dirs.store   = strpathcombine(g_dirs.config, "store");
    g_dirs.kitchen = strpathcombine(g_dirs.root, "spaces");
    if (g_dirs.fridge == NULL || g_dirs.store == NULL || g_dirs.kitchen == NULL) {
        VLOG_FATAL("dirs", "failed to allocate memory for paths\n");
    }
    return __ensure_chef_global_dirs();
}

// bakectl is running inside the container - meaning
// we don't actually need to do that much
static int __initialize_bakectl(void)
{
    if (g_dirs.real_user != 0) {
        VLOG_ERROR("dirs", "running bakectl as non-root user is not currently supported\n");
        return -1;
    }

    g_dirs.root   = __strdup_fail("/chef");
    g_dirs.config = __strdup_fail("/chef/config");
    g_dirs.fridge = __strdup_fail("/chef/fridge");
    g_dirs.store  = __strdup_fail("/chef/store");
    return __ensure_chef_global_dirs();
}

static int __initialize_bake(void)
{
    if (g_dirs.real_user == 0) {
        VLOG_ERROR("dirs", "running bake as root is not currently supported\n");
        return -1;
    }

    g_dirs.root = __common_user_directory();
    if (g_dirs.root == NULL) {
        VLOG_ERROR("dirs", "failed to resolve user root directory\n");
        return -1;
    }

    g_dirs.config  = __strdup_fail(g_dirs.root);
    g_dirs.fridge  = strpathcombine(g_dirs.root, "fridge");
    g_dirs.store   = strpathcombine(g_dirs.root, "store");
    g_dirs.kitchen = strpathcombine(g_dirs.root, "spaces");
    if (g_dirs.fridge == NULL || g_dirs.store == NULL || g_dirs.kitchen == NULL) {
        VLOG_ERROR("dirs", "failed to allocate memory for paths\n");
        return -1;
    }
    return __ensure_chef_user_dirs();
}

int chef_dirs_initialize(enum chef_dir_scope scope)
{
    uid_t realUser = __real_user();

    g_dirs.scope = scope;
    g_dirs.real_user = realUser;

    switch (scope) {
        case CHEF_DIR_SCOPE_DAEMON:
            return __initialize_daemon();
        case CHEF_DIR_SCOPE_BAKECTL:
            return __initialize_bakectl();
        case CHEF_DIR_SCOPE_BAKE:
            return __initialize_bake();
        default:
            VLOG_ERROR("dirs", "unrecognized scope\n");
            return -1;
    }
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

const char* chef_dirs_rootfs(const char* uuid)
{
    if (g_dirs.kitchen == NULL) {
        VLOG_ERROR("dirs", "chef_dirs_rootfs() is not available\n");
        return NULL;
    }
    if (uuid != NULL) {
        return strpathcombine(g_dirs.kitchen, uuid);
    }
    return g_dirs.kitchen;
}

char* chef_dirs_rootfs_new(const char* uuid)
{
    char*        kitchen;
    unsigned int mode = 0755;

    if (g_dirs.kitchen == NULL) {
        VLOG_ERROR("dirs", "chef_dirs_rootfs_new() is not available\n");
        return NULL;
    }
    
    kitchen = strpathcombine(g_dirs.kitchen, uuid);
    if (kitchen == NULL) {
        VLOG_ERROR("dirs", "chef_dirs_rootfs_new: failed to allocate memory for path\n");
        return NULL;
    }

    // If we are in daemon (root) mode we use different permissions
    if (g_dirs.real_user == 0) {
        mode = 0777;
    }

    if (__mkdir_as(kitchen, mode, g_dirs.real_user, g_dirs.real_user)) {
        VLOG_ERROR("dirs", "chef_dirs_rootfs_new: failed to create %s (mode: %o)\n", kitchen, mode);
        free(kitchen);
        return NULL;
    }
    return kitchen;
}

const char* chef_dirs_config(void)
{
    if (g_dirs.config == NULL) {
        VLOG_ERROR("dirs", "chef_dirs_config() is not available\n");
        return NULL;
    }
    return g_dirs.config;
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
