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

#include <chef/recipe.h>
#include <stdio.h>
#include <string.h>
#include "commands.h"

extern const char* g_baseYaml;

static void __print_help(void)
{
    printf("Usage: bake init [options]\n");
    printf("\n");
    printf("Options:\n");
    printf("  -n, --name\n");
    printf("      Name of the recipe\n");
}

static int __write_recipe(char* output)
{
    FILE* file;

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

    fwrite(g_baseYaml, strlen(g_baseYaml), 1, file);
    fclose(file);
    printf("%s created.\n", output);
    return 0;
}

int init_main(int argc, char** argv, char** envp, struct bake_command_options* options)
{
    char* output = "recipe.yaml";

    (void)envp;
    (void)options;

    for (int i = 1; i < argc; i++) {
        if (__cli_is_help_switch(argv[i])) {
            __print_help();
            return 0;
        }
        if (!__parse_string_switch(argv, argc, &i, "-n", 2, "--name", 6, NULL, &output)) {
            if (output == NULL) {
                fprintf(stderr, "bake: missing recipe name for --name\n");
                return -1;
            }
            continue;
        }
        fprintf(stderr, "bake: unknown option %s\n", argv[i]);
        __print_help();
        return -1;
    }
    return __write_recipe(output);
}
