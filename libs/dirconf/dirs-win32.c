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

#include <chef/dirs.h>
#include <chef/platform.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <vlog.h>

static struct {
    enum chef_dir_scope scope;

    // per-user / per-install
    const char* root;
    const char* store;
    const char* cache;
    const char* kitchen;

    // global
    const char* config;
} g_dirs = { 0, NULL, NULL, NULL, NULL, NULL };

static char* __strdup_fail(const char* str)
{
    char* copy = platform_strdup(str);
    if (copy == NULL) {
        VLOG_FATAL("dirs", "failed to allocate memory for %s\n", str);
    }
    return copy;
}

static int __ensure_dirs(const char* const* paths)
{
    for (int i = 0; paths[i] != NULL; i++) {
        const char* path = paths[i];
        if (path == NULL) {
            continue;
        }
        if (platform_mkdir(path)) {
            VLOG_ERROR("dirs", "failed to create %s\n", path);
            return -1;
        }
    }
    return 0;
}

static char* __user_root_directory(void)
{
    // We use LocalAppData (platform_getuserdir) so the user does not need
    // admin permissions and we avoid cluttering the home directory.
    char buffer[PATH_MAX] = { 0 };
    if (platform_getuserdir(&buffer[0], sizeof(buffer) - 1)) {
        return NULL;
    }
    return strpathcombine(&buffer[0], "chef");
}

static char* __user_config_directory(void)
{
    // Prefer RoamingAppData for small configuration so it can roam in domain
    // environments. This is the common Windows convention.
    const char* roaming = getenv("APPDATA");
    if (roaming != NULL && roaming[0] != 0) {
        return strpathcombine(roaming, "chef");
    }

    // Fallback: keep config alongside the LocalAppData root.
    return __user_root_directory();
}

static char* __global_root_directory(void)
{
    // Prefer ProgramData for daemon/service scope.
    // We avoid depending on Windows SDK headers here; the env var exists on Windows.
    const char* programData = getenv("ProgramData");
    if (programData == NULL || programData[0] == 0) {
        programData = "C:\\ProgramData";
    }
    return strpathcombine(programData, "chef");
}

static int __initialize_daemon(void)
{
    g_dirs.root = __global_root_directory();
    if (g_dirs.root == NULL) {
        VLOG_ERROR("dirs", "failed to resolve global root directory\n");
        return -1;
    }

    g_dirs.config  = strpathcombine(g_dirs.root, "config");
    g_dirs.store   = strpathcombine(g_dirs.root, "store");
    g_dirs.cache   = strpathcombine(g_dirs.root, "cache");
    g_dirs.kitchen = strpathcombine(g_dirs.root, "spaces");
    if (g_dirs.config == NULL || g_dirs.store == NULL || g_dirs.cache == NULL || g_dirs.kitchen == NULL) {
        VLOG_FATAL("dirs", "failed to allocate memory for paths\n");
    }

    {
        const char* paths[] = { g_dirs.root, g_dirs.config, g_dirs.store, g_dirs.cache, g_dirs.kitchen, NULL };
        return __ensure_dirs(paths);
    }
}

static int __initialize_bakectl(void)
{
    // bakectl runs inside the container. Keep the same conceptual layout as Linux.
    // This path is also plausible for Windows-based containers.
    g_dirs.root   = __strdup_fail("C:\\chef");
    g_dirs.config = __strdup_fail("C:\\chef\\config");
    g_dirs.store  = __strdup_fail("C:\\chef\\store");
    g_dirs.cache  = __strdup_fail("C:\\chef\\cache");
    g_dirs.kitchen = __strdup_fail("C:\\chef\\spaces");

    {
        const char* paths[] = { g_dirs.root, g_dirs.config, g_dirs.store, g_dirs.cache, g_dirs.kitchen, NULL };
        return __ensure_dirs(paths);
    }
}

static int __initialize_bake(void)
{
    g_dirs.root = __user_root_directory();
    if (g_dirs.root == NULL) {
        VLOG_ERROR("dirs", "failed to resolve user root directory\n");
        return -1;
    }

    // Windows convention: config in RoamingAppData, data/cache in LocalAppData.
    g_dirs.config = __user_config_directory();
    if (g_dirs.config == NULL) {
        VLOG_ERROR("dirs", "failed to resolve user config directory\n");
        return -1;
    }

    // store/cache/spaces remain under the LocalAppData root.
    g_dirs.store   = strpathcombine(g_dirs.root, "store");
    g_dirs.cache   = strpathcombine(g_dirs.root, "cache");
    g_dirs.kitchen = strpathcombine(g_dirs.root, "spaces");
    if (g_dirs.store == NULL || g_dirs.cache == NULL || g_dirs.kitchen == NULL) {
        VLOG_ERROR("dirs", "failed to allocate memory for paths\n");
        return -1;
    }

    {
        const char* paths[] = { g_dirs.root, g_dirs.config, g_dirs.store, g_dirs.cache, g_dirs.kitchen, NULL };
        return __ensure_dirs(paths);
    }
}

int chef_dirs_initialize(enum chef_dir_scope scope)
{
    g_dirs.scope = scope;

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

const char* chef_dirs_store(void)
{
    if (g_dirs.store == NULL) {
        VLOG_ERROR("dirs", "chef_dirs_store() is not available\n");
        return NULL;
    }
    return g_dirs.store;
}

const char* chef_dirs_cache(void)
{
    if (g_dirs.cache == NULL) {
        VLOG_ERROR("dirs", "chef_dirs_cache() is not available\n");
        return NULL;
    }
    return g_dirs.cache;
}

const char* chef_dirs_rootfs(const char* uuid)
{
    static char combined[PATH_MAX];

    if (g_dirs.kitchen == NULL) {
        VLOG_ERROR("dirs", "chef_dirs_rootfs() is not available\n");
        return NULL;
    }

    if (uuid == NULL || uuid[0] == 0) {
        return g_dirs.kitchen;
    }

    {
        size_t len = strlen(g_dirs.kitchen);
        int    n;
        if (len > 0 && g_dirs.kitchen[len - 1] == CHEF_PATH_SEPARATOR) {
            n = snprintf(combined, sizeof(combined), "%s%s", g_dirs.kitchen, uuid);
        } else {
            n = snprintf(combined, sizeof(combined), "%s" CHEF_PATH_SEPARATOR_S "%s", g_dirs.kitchen, uuid);
        }
        if (n < 0 || (size_t)n >= sizeof(combined)) {
            errno = ENAMETOOLONG;
            return NULL;
        }
    }

    return combined;
}

char* chef_dirs_rootfs_alloc(const char* uuid)
{
    if (g_dirs.kitchen == NULL) {
        VLOG_ERROR("dirs", "chef_dirs_rootfs_alloc() is not available\n");
        return NULL;
    }
    if (uuid == NULL || uuid[0] == 0) {
        return platform_strdup(g_dirs.kitchen);
    }
    return strpathcombine(g_dirs.kitchen, uuid);
}

char* chef_dirs_rootfs_new(const char* uuid)
{
    char* rootfs;

    if (g_dirs.kitchen == NULL) {
        VLOG_ERROR("dirs", "chef_dirs_rootfs_new() is not available\n");
        return NULL;
    }

    rootfs = strpathcombine(g_dirs.kitchen, uuid);
    if (rootfs == NULL) {
        VLOG_ERROR("dirs", "chef_dirs_rootfs_new: failed to allocate memory for path\n");
        return NULL;
    }

    if (platform_mkdir(rootfs)) {
        VLOG_ERROR("dirs", "chef_dirs_rootfs_new: failed to create %s\n", rootfs);
        free(rootfs);
        return NULL;
    }
    return rootfs;
}

const char* chef_dirs_config(void)
{
    if (g_dirs.config == NULL) {
        VLOG_ERROR("dirs", "chef_dirs_config() is not available\n");
        return NULL;
    }
    return g_dirs.config;
}

FILE* chef_dirs_open_temp_file(const char* name, const char* ext, char** rpath)
{
    char  guid[40] = { 0 };
    const char* safeName = (name != NULL && name[0] != 0) ? name : "chef";
    const char* safeExt = (ext != NULL && ext[0] != 0) ? ext : "tmp";
    const char* tmpBase;
    char  fileName[128] = { 0 };
    char* filePath;
    FILE* stream;
    static int seeded = 0;

    if (safeExt[0] == '.') {
        safeExt++;
    }

    if (!seeded) {
        srand((unsigned int)time(NULL));
        seeded = 1;
    }

    tmpBase = getenv("TEMP");
    if (tmpBase == NULL || tmpBase[0] == 0) {
        tmpBase = getenv("TMP");
    }
    if (tmpBase == NULL || tmpBase[0] == 0) {
        tmpBase = getenv("TMPDIR");
    }
    if (tmpBase == NULL || tmpBase[0] == 0) {
#if CHEF_ON_WINDOWS
        tmpBase = "C:\\Windows\\Temp";
#else
        tmpBase = "/tmp";
#endif
    }

    platform_guid_new_string(guid);
    if (snprintf(fileName, sizeof(fileName), "%s-%s.%s", safeName, guid, safeExt) >= (int)sizeof(fileName)) {
        errno = ENAMETOOLONG;
        return NULL;
    }

    filePath = strpathcombine(tmpBase, fileName);
    if (filePath == NULL) {
        return NULL;
    }

    stream = fopen(filePath, "w+");
    if (stream == NULL) {
        VLOG_ERROR("dirs", "failed to open path %s for writing\n", filePath);
        free(filePath);
        return NULL;
    }

    if (rpath != NULL) {
        *rpath = filePath;
    } else {
        free(filePath);
    }

    if (rpath != NULL && *rpath == NULL) {
        fclose(stream);
        return NULL;
    }

    return stream;
}
