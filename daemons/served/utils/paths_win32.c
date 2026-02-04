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

#include <utils.h>
#include <chef/platform.h>
#include <errno.h>

#include <windows.h>
#ifndef PATH_MAX
#define PATH_MAX MAX_PATH
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vlog.h>

static char* g_served_root = NULL;

void utils_path_set_root(const char* root)
{
    free(g_served_root);
    g_served_root = platform_strdup(root);
}

char* served_paths_path(const char* path)
{
    return strpathcombine(g_served_root, path);
}

char* utils_path_pack(const char* publisher, const char* package)
{
    char buffer[PATH_MAX];
    snprintf(
        &buffer[0], sizeof(buffer),
        "chef\\packs\\%s-%s.pack",
        publisher, package
    );
    return served_paths_path(&buffer[0]);
}

char* utils_path_data(const char* publisher, const char* package, int revision)
{
    char buffer[PATH_MAX];
    snprintf(
        &buffer[0], sizeof(buffer), 
        "chef\\data\\%s-%s\\%i", 
        publisher, package, revision
    );
    return served_paths_path(&buffer[0]);
}

char* utils_path_command_wrapper(const char* name)
{
    char buffer[PATH_MAX];
    snprintf(
        &buffer[0], sizeof(buffer), 
        "chef\\bin\\%s.cmd",
        name
    );
    return served_paths_path(&buffer[0]);
}

char* utils_path_state_db(void)
{
    char buffer[PATH_MAX];
    snprintf(
        &buffer[0], sizeof(buffer),
        "chef\\state.db"
    );
    return served_paths_path(&buffer[0]);
}

char* utils_path_binary_path(void)
{
    char buffer[PATH_MAX];
    snprintf(
        &buffer[0], sizeof(buffer),
        "chef\\bin"
    );
    return served_paths_path(&buffer[0]);
}

char* utils_path_packs_root(void)
{
    char buffer[PATH_MAX];
    snprintf(
        &buffer[0], sizeof(buffer),
        "chef\\packs"
    );
    return served_paths_path(&buffer[0]);
}

char* utils_path_data_root(void)
{
    char buffer[PATH_MAX];
    snprintf(
        &buffer[0], sizeof(buffer),
        "chef\\data"
    );
    return served_paths_path(&buffer[0]);
}
