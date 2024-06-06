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

#include <ctype.h>
#include <chef/platform.h>
#include <chef/recipe.h>
#include <string.h>
#include <vlog.h>

struct recipe_cache_package {

};

struct recipe_cache {
    char uuid[40];
};

static const char*         g_uuidFmt = "xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx";
static const char*         g_hex = "0123456789ABCDEF-";
static struct recipe_cache g_cache = { 0 };

static void __generate_cache_uuid(char uuid[40])
{
    int length = strlen(g_uuidFmt);
    for (int i = 0; i < (length + 1); i++) {
        int r = rand() % 16;
        char c = ' ';   
        
        switch (g_uuidFmt[i]) {
            case 'x' : { c = g_hex[r]; } break;
            case 'y' : { c = g_hex[r & 0x03 | 0x08]; } break;
            case '-' : { c = '-'; } break;
            case '4' : { c = '4'; } break;
        }
        uuid[i] = (i < length) ? c : 0x00;
    }
}

static int __load_cache(struct recipe_cache* cache)
{

}

static int __save_cache(struct recipe_cache* cache)
{
    
}

int recipe_cache_initialize(struct recipe* current)
{
    int status;

    // initialize the random counter for guid's
    srand(clock());

    status = __load_cache(&g_cache);
    if (status) {
        VLOG_ERROR("cache", "failed to load or initialize the recipe cache\n");
        return status;
    }

    return 0;
}

const char* recipe_cache_uuid(void)
{
    return &g_cache.uuid[0];
}

void recipe_cache_calculate_package_changes()
{

}

void recipe_cache_commit_package_changes()
{
    
}
