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

#include <parse.h>
#include <recipe.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int init_main(int argc, char** argv, struct recipe* recipe);
extern int fetch_main(int argc, char** argv, struct recipe* recipe);
extern int pack_main(int argc, char** argv, struct recipe* recipe);

struct command_handler {
    char* name;
    int (*handler)(int argc, char** argv, struct recipe* recipe);
};

static struct command_handler g_commands[] = {
    { "init",  init_main },
    { "fetch", fetch_main },
    { "pack",  pack_main }
};

static void __print_help(void)
{
    printf("Usage: bake <command> [options]\n");
    printf("\n");
    printf("Commands:\n");
    printf("  init        initializes a new recipe in the current directory\n");
    printf("  fetch       refreshes/fetches all ingredients\n");
    printf("  pack        builds the project\n");
    printf("\n");
    printf("Options:\n");
    printf("  -h, --help\n");
    printf("      Print this help message\n");
    printf("  -v, --version\n");
    printf("      Print the version of bake\n");
}

static struct command_handler* __get_command(const char* command)
{
    for (int i = 0; i < sizeof(g_commands) / sizeof(struct command_handler); i++) {
        if (!strcmp(command, g_commands[i].name)) {
            return &g_commands[i];
        }
    }
    return NULL;
}

static int __read_recipe(void** bufferOut, size_t* lengthOut)
{
    FILE*  file;
    void*  buffer;
    size_t size;

    file = fopen("recipe.yaml", "r");
    if (!file) {
        return -1;
    }

    fseek(file, 0, SEEK_END);
    size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    buffer = malloc(size);
    if (!buffer) {
        fclose(file);
        return -1;
    }

    fread(buffer, size, 1, file);
    fclose(file);

    *bufferOut = buffer;
    *lengthOut = size;
    return 0;
}

int main(int argc, char** argv)
{
    struct command_handler* command = &g_commands[2];
    struct recipe*          recipe  = NULL;
    void*                   buffer;
    size_t                  length;
    int                     result;

    if (argc > 1) {
        if (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")) {
            __print_help();
            return 0;
        }

        command = __get_command(argv[1]);
        if (!command) {
            fprintf(stderr, "bake: invalid command %s\n", argv[1]);
            return -1;
        }
    }

    result = __read_recipe(&buffer, &length);
    if (!result) {
        result = recipe_parse(buffer, length, &recipe);
        free(buffer);
        if (result != 0) {
            fprintf(stderr, "bake: failed to parse recipe\n");
            return result;
        }
    }

    result = command->handler(argc, argv, recipe);
    if (result != 0) {
        return result;
    }
    return 0;
}
