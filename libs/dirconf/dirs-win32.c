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

int chef_dirs_initialize(void)
{
    int  status;
    char buffer[PATH_MAX] = { 0 };

    status = platform_getuserdir(&buffer[0], sizeof(buffer) - 1);
    if (status) {
        VLOG_ERROR("fridge", "fridge_initialize: failed to resolve user homedir\n");
        return -1;
    }
    return 0;
}

const char* chef_dirs_root(void)
{
    return NULL;
}

const char* chef_dirs_fridge(void)
{
    return NULL;
}

const char* chef_dirs_store(void)
{
    return NULL;
}

const char* chef_dirs_kitchen(const char* uuid)
{
    return NULL;
}

const char* chef_dirs_ensure(const char* path)
{
    return NULL;
}

FILE* chef_dirs_contemporary_file(char** rpath)
{
    // GetTempPath
    // GetTempFileName
    return NULL;
}
