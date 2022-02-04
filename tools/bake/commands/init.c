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

#include <recipe.h>
#include <stdio.h>
#include <string.h>

extern const char* g_appYaml;
extern const char* g_libraryYaml;

static void __print_help(void)
{
    printf("Usage: bake init [options]\n");
    printf("\n");
    printf("Options:\n");
    printf("  -t, --type\n");
    printf("      Possible recipe types are {lib, app}\n");
}

static int __write_recipe(enum recipe_type type)
{
    const char* recipe = NULL;
    FILE*       file;

    switch (type) {
        case RECIPE_TYPE_LIBRARY:
            recipe = g_libraryYaml;
            break;
        case RECIPE_TYPE_APPLICATION:
            recipe = g_appYaml;
            break;
    }

    file = fopen("recipe.yaml", "r");
    if (file) {
        fclose(file);
        printf("bake: recipe already exists, please remove it first.\n");
        return -1;
    }

    file = fopen("recipe.yaml", "w");
    if (!file) {
        printf("bake: failed to create recipe.yaml\n");
        return -1;
    }

    fwrite(recipe, strlen(recipe), 1, file);
    fclose(file);
    printf("recipe.yaml created.\n");
    return 0;
}

int init_main(int argc, char** argv, struct recipe* recipe)
{
    enum recipe_type type = RECIPE_TYPE_LIBRARY;

    (void)recipe;

    if (argc > 2) {
        for (int i = 2; i < argc; i++) {
            if (!strcmp(argv[2], "-h") || !strcmp(argv[2], "--help")) {
                __print_help();
                return 0;
            }
            else if (!strcmp(argv[2], "-t") || !strcmp(argv[2], "--type")) {
                if (!strcmp(argv[3], "lib")) {
                    type = RECIPE_TYPE_LIBRARY;
                }
                else if (!strcmp(argv[3], "app")) {
                    type = RECIPE_TYPE_APPLICATION;
                }
                else {
                    printf("bake: unknown recipe type: %s\n", argv[3]);
                    return -1;
                }
            }
        }
    }
    return __write_recipe(type);
}
