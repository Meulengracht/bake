/*
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
#include <chef/package.h>
#include <string.h>
#include <utils.h>
#include <vlog.h>

char** utils_split_package_name(const char* name)
{
    // split the publisher/package
    int    namesCount = 0;
    char** names = strsplit(name, '/');
    if (names == NULL) {
        VLOG_ERROR("store", "__find_package_in_inventory: invalid package naming '%s' (must be publisher/package)\n", name);
        return NULL;
    }

    while (names[namesCount] != NULL) {
        namesCount++;
    }

    if (namesCount != 2) {
        VLOG_ERROR("store", "__find_package_in_inventory: invalid package naming '%s' (must be publisher/package)\n", name);
        strsplit_free(names);
        return NULL;
    }
    return names;
}

char* utils_base_to_store_id(const char* base)
{
    char   storeID[CHEF_PACKAGE_ID_LENGTH_MAX] = { 0 };
    size_t len = strlen(base);
    strcpy(&storeID[0], "vali/");

    for (size_t i = 0, j = 5; i < len; i++, j++) {
        if (base[i] == ':') {
            storeID[j] = '-';
        } else {
            storeID[j] = base[i];
        }
    }
    return platform_strdup(&storeID[0]);
}
