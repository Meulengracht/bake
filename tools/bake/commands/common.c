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

#include <chef/platform.h>
#include <chef/recipe.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chef-config.h"
#include "commands.h"

static void __free_string_item(void* item)
{
    free(item);
}

static int __is_osbase(const char* name)
{
    if (strcmp(name, "vali/linux-1") == 0) {
        return 0;
    }
    return -1;
}

static int __add_ingredient(struct recipe* recipe, const char* name)
{
    struct recipe_ingredient* ingredient;

    ingredient = malloc(sizeof(struct recipe_ingredient));
    if (ingredient == NULL) {
        return -1;
    }

    memset(ingredient, 0, sizeof(struct recipe_ingredient));
    ingredient->name = platform_strdup(name);
    ingredient->type = RECIPE_INGREDIENT_TYPE_HOST;
    if (ingredient->name == NULL) {
        free(ingredient);
        return -1;
    }

    ingredient->channel = "devel";
    list_add(&recipe->environment.host.ingredients, &ingredient->list_header);
    return 0;
}

static int __add_osbase(struct recipe* recipe)
{
    char nameBuffer[32];

    snprintf(&nameBuffer[0], sizeof(nameBuffer), "vali/%s-1", CHEF_PLATFORM_STR);
    return __add_ingredient(recipe, &nameBuffer[0]);
}

static int __add_implicit_ingredients(struct recipe* recipe)
{
    struct list_item* i;
    int               needsOs = recipe->environment.host.base;

    list_foreach(&recipe->environment.host.ingredients, i) {
        struct recipe_ingredient* ingredient = (struct recipe_ingredient*)i;
        if (__is_osbase(ingredient->name) == 0) {
            needsOs = 0;
        }
    }

#if defined(__MOLLENOS__)
    if (needsOs && __add_osbase(recipe)) {
        return -1;
    }
#endif
    return 0;
}

static const char* __find_default_recipe(void)
{
    struct platform_stat stats;

    if (platform_stat("chef/recipe.yaml", &stats) == 0) {
        return "chef/recipe.yaml";
    }
    if (platform_stat("recipe.yaml", &stats) == 0) {
        return "recipe.yaml";
    }
    return NULL;
}

void bake_command_options_reset(struct bake_command_options* options)
{
    if (options == NULL) {
        return;
    }

    recipe_destroy(options->recipe);
    options->recipe = NULL;
    list_destroy(&options->architectures, __free_string_item);
    options->recipe_path = NULL;
    options->input_path = NULL;
    options->platform = NULL;
}

int bake_command_parse_target_option(int argc, char** argv, int* index, struct bake_command_options* options)
{
    if (!__parse_string_switch(argv, argc, index, "-p", 2, "--platform", 10, NULL, (char**)&options->platform)) {
        return CLI_PARSE_RESULT_HANDLED;
    }
    if (!__parse_stringv_switch(argv, argc, index, "-a", 2, "--archs", 7, NULL, &options->architectures)) {
        return CLI_PARSE_RESULT_HANDLED;
    }
    return CLI_PARSE_RESULT_UNHANDLED;
}

int bake_command_load_recipe(struct bake_command_options* options)
{
    void*  buffer = NULL;
    size_t length = 0;
    int    status;

    if (options == NULL) {
        return -1;
    }

    if (options->recipe_path == NULL) {
        options->recipe_path = __find_default_recipe();
    }
    if (options->recipe_path == NULL) {
        fprintf(stderr, "bake: no recipe provided\n");
        return -1;
    }

    status = platform_readfile(options->recipe_path, &buffer, &length);
    if (status) {
        fprintf(stderr, "bake: failed to read recipe: %s\n", options->recipe_path);
        return -1;
    }

    status = recipe_parse(buffer, length, &options->recipe);
    free(buffer);
    if (status) {
        fprintf(stderr, "bake: failed to parse recipe\n");
        return status;
    }

    status = __add_implicit_ingredients(options->recipe);
    if (status) {
        fprintf(stderr, "bake: failed to add implicit ingredients\n");
        goto cleanup;
    }

    status = recipe_ensure_target(options->recipe, &options->platform, &options->architectures);
    if (status) {
        goto cleanup;
    }
    return 0;

cleanup:
    recipe_destroy(options->recipe);
    options->recipe = NULL;
    return status;
}