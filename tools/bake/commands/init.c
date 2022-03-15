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
extern const char* g_ingredientYaml;

static void __print_help(void)
{
    printf("Usage: bake init [options]\n");
    printf("\n");
    printf("Options:\n");
    printf("  -t, --type\n");
    printf("      Possible recipe types are {ingredient, app, toolchain}\n");
    printf("  -n, --name\n");
    printf("      Name of the recipe\n");
}

static int __write_recipe(enum chef_package_type type, char* output)
{
    const char* recipe = NULL;
    FILE*       file;

    switch (type) {
        case CHEF_PACKAGE_TYPE_INGREDIENT:
            recipe = g_ingredientYaml;
            break;
        case CHEF_PACKAGE_TYPE_APPLICATION:
            recipe = g_appYaml;
            break;
        case CHEF_PACKAGE_TYPE_TOOLCHAIN:
            recipe = g_appYaml;
            break;
    }

    if (recipe == NULL) {
        fprintf(stderr, "bake: unknown recipe type\n");
        return -1;
    }

    file = fopen(output, "r");
    if (file) {
        fclose(file);
        fprintf(stderr, "bake: recipe already exists, please remove it first.\n");
        return -1;
    }

    file = fopen(output, "w");
    if (!file) {
        fprintf(stderr, "bake: failed to create %s\n", output);
        return -1;
    }

    fwrite(recipe, strlen(recipe), 1, file);
    fclose(file);
    printf("%s created.\n", output);
    return 0;
}

int init_main(int argc, char** argv, char** envp, struct recipe* recipe)
{
    enum chef_package_type type   = CHEF_PACKAGE_TYPE_INGREDIENT;
    char*                  output = "recipe.yaml";

    (void)recipe;

    if (argc > 2) {
        for (int i = 2; i < argc; i++) {
            if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
                __print_help();
                return 0;
            }
            else if (!strcmp(argv[i], "-t") || !strcmp(argv[i], "--type")) {
                if (i + 1 < argc) {
                    if (!strcmp(argv[i + 1], "ingredient")) {
                        type = CHEF_PACKAGE_TYPE_INGREDIENT;
                    }
                    else if (!strcmp(argv[i + 1], "toolchain")) {
                        type = CHEF_PACKAGE_TYPE_TOOLCHAIN;
                    }
                    else if (!strcmp(argv[i + 1], "app")) {
                        type = CHEF_PACKAGE_TYPE_APPLICATION;
                    }
                    else {
                        printf("bake: invalid recipe type %s\n", argv[i + 1]);
                        return -1;
                    }
                }
                else {
                    printf("bake: missing recipe type\n");
                    return -1;
                }
            }
            else if (!strcmp(argv[i], "-n") || !strcmp(argv[i], "--name")) {
                if (i + 1 < argc) {
                    output = argv[i + 1];
                }
                else {
                    printf("bake: missing recipe name for --name\n");
                    return -1;
                }
            }
        }
    }
    return __write_recipe(type, output);
}
