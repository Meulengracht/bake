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

#include <chef/platform.h>
#include <windows.h>
#include <sys/stat.h>

int platform_stat(const char* path, struct platform_stat* stats)
{
    struct _stat st;
    DWORD attributes;

    if (path == NULL || stats == NULL) {
        return -1;
    }

    if (_stat(path, &st) != 0) {
        return -1;
    }

    stats->size = st.st_size;
    
    // Get Windows file attributes to check for reparse points (symlinks)
    attributes = GetFileAttributesA(path);
    
    if (attributes != INVALID_FILE_ATTRIBUTES && 
        (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
        stats->type = PLATFORM_FILETYPE_SYMLINK;
    } else {
        switch (st.st_mode & S_IFMT) {
            case S_IFREG:
                stats->type = PLATFORM_FILETYPE_FILE;
                break;
            case S_IFDIR:
                stats->type = PLATFORM_FILETYPE_DIRECTORY;
                break;
            default:
                stats->type = PLATFORM_FILETYPE_UNKNOWN;
                break;
        }
    }

    // Windows permissions are more limited than Unix
    // Map Windows permissions to Unix-like permissions
    stats->permissions = 0;
    if (st.st_mode & S_IREAD) {
        stats->permissions |= 0444; // read for owner, group, others
    }
    if (st.st_mode & S_IWRITE) {
        stats->permissions |= 0222; // write for owner, group, others
    }
    if (st.st_mode & S_IEXEC) {
        stats->permissions |= 0111; // execute for owner, group, others
    }

    return 0;
}
