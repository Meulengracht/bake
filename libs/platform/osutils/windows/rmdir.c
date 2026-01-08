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


int platform_rmdir(const char *path) 
{
    WIN32_FIND_DATA findFileData;
    HANDLE hFind;
    char fullPath[MAX_PATH];
    int result = 0;

    snprintf(fullPath, sizeof(fullPath), "%s\\*", path);

    hFind = FindFirstFile(fullPath, &findFileData);

    if (hFind == INVALID_HANDLE_VALUE) {
        return -1;
    }

    do {
        if (strcmp(findFileData.cFileName, ".") == 0 || strcmp(findFileData.cFileName, "..") == 0) {
            continue;
        }

        snprintf(fullPath, sizeof(fullPath), "%s\\%s", path, findFileData.cFileName);

        if (findFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            result = RemoveDirectory(fullPath);
        } else if (!DeleteFile(fullPath)) {
                result = -1;
        }
    } while (!result && FindNextFile(hFind, &findFileData));

    FindClose(hFind);

    if (!result) {
        if (!RemoveDirectory(path)) {
            result = -1;
        }
    }

    return result;
}
