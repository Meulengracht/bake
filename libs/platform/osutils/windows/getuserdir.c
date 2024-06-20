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
#include <shlobj.h>
#include <string.h>

int platform_getuserdir(char* buffer, size_t length)
{
    WCHAR path[MAX_PATH];
    HRESULT result = SHGetFolderPathW(NULL, CSIDL_PROFILE, NULL, 0, path);

    if (result != S_OK) {
        return -1;
    }

    size_t convertedChars = 0;
    wcstombs_s(&convertedChars, buffer, length, path, _TRUNCATE);

    return 0;
}
