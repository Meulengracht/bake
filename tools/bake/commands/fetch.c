/**
 * Copyright 2022, Philip Meulengracht
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

#include <chef/client.h>
#include <libfridge.h>
#include <recipe.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void __print_help(void)
{
    printf("Usage: bake fetch [options]\n");
    printf("\n");
    printf("Options:\n");
    printf("  -h, --help\n");
    printf("      Shows this help message\n");
}

int fetch_main(int argc, char** argv, char** envp, struct recipe* recipe)
{
    struct list_item* item;
    int               status;

    // handle individual help command
    if (argc > 2) {
        for (int i = 2; i < argc; i++) {
            if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
                __print_help();
                return 0;
            }
        }
    }

    if (recipe == NULL) {
        fprintf(stderr, "bake: no recipe specified\n");
        return -1;
    }
    
    status = fridge_initialize();
    if (status != 0) {
        fprintf(stderr, "bake: failed to initialize fridge\n");
        return -1;
    }
    atexit(fridge_cleanup);

    status = chefclient_initialize();
    if (status != 0) {
        fprintf(stderr, "bake: failed to initialize chef client\n");
        return -1;
    }
    atexit(chefclient_cleanup);

    // iterate through all ingredients
    printf("bake: fetching %i ingredients\n", recipe->ingredients.count);
    for (item = recipe->ingredients.head; item != NULL; item = item->next) {
        struct recipe_ingredient* ingredient = (struct recipe_ingredient*)item;
        
        // fetch the ingredient
        status = fridge_store_ingredient(&ingredient->ingredient);
        if (status != 0) {
            fprintf(stderr, "bake: failed to fetch ingredient %s\n", ingredient->ingredient.name);
        }
    }
    return 0;
}
