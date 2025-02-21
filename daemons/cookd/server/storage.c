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

#if defined(__linux__)
#define _GNU_SOURCE

#include <linux/limits.h>
#include <sys/mount.h>
#include <stdlib.h>
#include <vlog.h>

char* storage_build_new(const char* id, unsigned int mb)
{
    const char* basedir;
	char* mountpoint;
	char  path[PATH_MAX] = { 0 };
    char  options[128] = { 0 };
    int   status;

	basedir = getenv("TMPDIR");
	if (basedir == NULL){
		basedir = "/tmp";
	}

	snprintf(&path[0], sizeof(path) - 1, "%s/cookd_build_XXXXXX", basedir);

	mountpoint = mkdtemp(&path[0]);
	if (!mountpoint){
        VLOG_ERROR("storage", "storage_build_new: failed to create temporary path %s\n", &path[0]);
		return NULL;
	}

	snprintf(&options[0], sizeof(options) - 1, "size=%uM,uid=0,gid=0,mode=700", mb);
	status = mount("tmpfs", mountpoint, "tmpfs", 0, options);
	if (status){
        VLOG_ERROR("storage", "storage_build_new: failed to mount tmpfs at %s\n", &path[0]);
		if (remove(mountpoint)) {
            VLOG_ERROR("storage", "storage_build_delete: failed to remove mount directory %s\n", path);
        }
		return NULL;
	}
	return strdup(mountpoint);
}

void storage_build_delete(char* path)
{
    if (umount(path)) {
        VLOG_ERROR("storage", "storage_build_delete: failed to unmount tmpfs at %s\n", path);
        return;
    }
    if (remove(path)) {
        VLOG_ERROR("storage", "storage_build_delete: failed to remove mount directory %s\n", path);
    }
    free(path);
}

#else
#include <chef/dirs.h>
#include <chef/platform.h>

// default to hdd
char* build_storage(const char* id, unsigned int mb)
{
    return platform_strdup(chef_dirs_root());
}

void storage_build_delete(char* path)
{
    free(path);
}

#endif
