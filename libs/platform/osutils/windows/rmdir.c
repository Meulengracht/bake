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
#include <stdio.h>
#include <Windows.h>


int platform_rmdir(const char *path) {
    WIN32_FIND_DATA find_file_data;
    HANDLE hFind;
    char full_path[MAX_PATH];
    int r = 0;

    snprintf(full_path, sizeof(full_path), "%s\\*", path);

    hFind = FindFirstFile(full_path, &find_file_data);

    if (hFind == INVALID_HANDLE_VALUE) {
        return -1;
    }

    do {
        if (strcmp(find_file_data.cFileName, ".") == 0 || strcmp(find_file_data.cFileName, "..") == 0) {
            continue;
        }

        snprintf(full_path, sizeof(full_path), "%s\\%s", path, find_file_data.cFileName);

        if (find_file_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            r = remove_directory(full_path);
        } else {
            if (!DeleteFile(full_path)) {
                r = -1;
            }
        }
    } while (!r && FindNextFile(hFind, &find_file_data));

    FindClose(hFind);

    if (!r) {
        if (!RemoveDirectory(path)) {
            r = -1;
        }
    }

    return r;
}
