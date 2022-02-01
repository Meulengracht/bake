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

extern int init_main(int argc, char** argv, struct recipe* recipe);
extern int fetch_main(int argc, char** argv, struct recipe* recipe);
extern int pack_main(int argc, char** argv, struct recipe* recipe);

struct command_handler {
    char* name;
    int (*handler)(int argc, char** argv, struct recipe* recipe);
};

static struct command_handler* g_commands[] = {
    { "init", init_main },
    { "fetch", fetch_main },
    { "pack", pack_main }
};

static struct command_handler* __get_command(const char* command)
{
    for (int i = 0; i < sizeof(g_commands) / sizeof(char*); i++) {
        if (strcmp(command, g_commands[i]) == 0) {
            return g_commands[i];
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
        fprintf(stderr, "bake: failed to open recipe.yaml\n");
        return -1;
    }

    fseek(file, 0, SEEK_END);
    size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    buffer = malloc(size);
    if (!buffer) {
        fprintf(stderr, "bake: failed to allocate memory for recipe\n");
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
    struct command_handler* command = g_commands[2];
    struct recipe*          recipe;
    void*                   buffer;
    size_t                  length;
    int                     result;

    if (argc > 1) {
        command = __get_command(argv[1]);
        if (!command) {
            fprintf(stderr, "bake: invalid command %s\n", argv[1]);
            return -1;
        }
    }

    result = __read_recipe(&buffer, &length);
    if (result != 0) {
        return result;
    }

    result = recipe_parse(buffer, length, &recipe);
    if (result != 0) {
        return result;
    }

    result = command->handler(argc, argv, recipe);
    if (result != 0) {
        return result;
    }

    free(buffer);
    return 0;
}
