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

#include <errno.h>
#include <chef/platform.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

char* __prefix_path(const char* base, const char* path)
{
	// If 'path' is not an absolute path, then we assume it is relative to the
	// 'base' path, but the base path is a file path, so we need to remove the last
	// component
	if (path[0] != '/') {
		char*       result = calloc(1, strlen(base) + strlen(path) + 2);
		const char* last;
		if (result == NULL) {
			errno = ENOMEM;
			return NULL;
		}

		last = strrchr(base, CHEF_PATH_SEPARATOR);
		if (last == NULL) {
			size_t length = strlen(result);
			result[length] = CHEF_PATH_SEPARATOR;
			result[length + 1] = '\0';
		} else {
			strncpy(result, base, (last - base) + 1);
			result[(last - base) + 1] = '\0';
		}
		strcat(result, path);
		return result;
	}
	return strdup(path);
}

static int __create_dummy_dir_if_not_exists(const char* path)
{
	struct stat st;
	int         result;

	result = stat(path, &st);
	if (result && errno == ENOENT) {
		// rwxrwxr-x
		result = mkdir(path, 0775);
	}
	return result;
}

static int __create_dummy_file_if_not_exists(const char* path)
{
	struct stat st;
	int         result;

	result = stat(path, &st);
	if (result) {
		FILE* file = fopen(path, "w");
		if (file == NULL) {
			return -1;
		}
		fclose(file);
		result = 0;
	}
	return result;
}

int platform_symlink(const char* path, const char* target, int directory)
{
	char* targetFullPath;
	int   status;

	if (path == NULL || target == NULL) {
		errno = EINVAL;
		return -1;
	}

	// When creating the dummy path we need to do it at the actual path,
	// not just the relative
	targetFullPath = __prefix_path(path, target);
	if (targetFullPath == NULL) {
		fprintf(stderr, "symlink: failed to prefix path\n");
		return -1;
	}

	// it's actually a bit confusing, but path is actually the name of the
	// symlink, and target is the target of the symlink.
	if (directory) {
		status = __create_dummy_dir_if_not_exists(targetFullPath);
	} else {
		status = __create_dummy_file_if_not_exists(targetFullPath);
	}

	// At this point we can clean it up
	free(targetFullPath);
	if (status) {
		fprintf(stderr, "symlink: failed to create dummy target\n");
		return status;
	}

	status = symlink(target, path);
	if (status) {
		fprintf(stderr, "symlink: failed to link\n");
        // ignore it if it exists, in theory we would like to 'update it' if 
        // exists, but for now just ignore
        if (errno == EEXIST) {
			// update it to be the correct symlink
			status = unlink(path);
			if (status != 0) {
				return status;
			}

            status = symlink(target, path);
        }
	}
	return status;
}
