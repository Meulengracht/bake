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
    PWSTR path = NULL;
    size_t i;
    HRESULT hr = SHGetKnownFolderPath(&FOLDERID_LocalAppData, 0, NULL, &path);
    int result = -1;
    if (SUCCEEDED(hr)) {
        wcstombs_s(&i, buffer, length, path, length - 1);
        result = 0;
    }
    // Memory must be freed also when call fails
    CoTaskMemFree((LPVOID)path);
    return result;
}
