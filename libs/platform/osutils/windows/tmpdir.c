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
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

char* platform_tmpdir(void)
{
    char tempPath[MAX_PATH];
    char tempDir[MAX_PATH];
    DWORD result;
    int i;
    static int seeded = 0;

    // Seed random number generator once
    if (!seeded) {
        srand((unsigned int)time(NULL) ^ (unsigned int)GetCurrentProcessId());
        seeded = 1;
    }

    // Get the temp directory path
    result = GetTempPathA(MAX_PATH, tempPath);
    if (result == 0 || result > MAX_PATH) {
        return NULL;
    }

    // Generate a unique directory name using a GUID-like pattern
    for (i = 0; i < 100; i++) {
        unsigned int rand1 = (unsigned int)rand();
        unsigned int rand2 = (unsigned int)rand();
        unsigned int rand3 = (unsigned int)rand();
        unsigned int tick = (unsigned int)GetTickCount();
        
        snprintf(tempDir, MAX_PATH, "%schef-%08x%08x%08x%08x", 
                 tempPath, rand1, rand2, rand3, tick);

        // Try to create the directory
        if (CreateDirectoryA(tempDir, NULL)) {
            return platform_strdup(tempDir);
        }

        // If directory exists, try another name
        if (GetLastError() != ERROR_ALREADY_EXISTS) {
            return NULL;
        }
    }

    // Failed to create a unique directory after 100 attempts
    return NULL;
}
