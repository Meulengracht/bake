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

#include <errno.h>
#include <chef/list.h>
#include <chef/bake.h>
#include <chef/cache.h>
#include <chef/platform.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <vlog.h>

#include "commands.h"

static void __print_help(void)
{
    printf("Usage: bakectl init [options]\n");
    printf("\n");
    printf("Options:\n");
    printf("  -h,  --help\n");
    printf("      Shows this help message\n");
}

// ** /chef/project
// * This is mapped in by the host, and contains a RO path of the
// * source code for the project
// ** /chef/fridge
// * This is mapped by the host, and contains a RO path of the 
// * hosts fridge storage. We use this to load packs and toolchains
// * needed
static int __ensure_chef_directories(void)
{
    const char* platform = __get_platform();
    const char* architecture = __get_architecture();
    char        buffer[1024];

    VLOG_DEBUG("bake", "__ensure_hostdirs()\n");


    snprintf(&buffer[0], sizeof(buffer), "/chef/build/%s/%s", platform, architecture);
    if (platform_mkdir(&buffer[0])) {
        VLOG_ERROR("bake", "__ensure_hostdirs: failed to create %s\n", &buffer[0]);
        return -1;
    }

    snprintf(&buffer[0], sizeof(buffer), "/chef/ingredients/%s/%s", platform, architecture);
    if (platform_mkdir(&buffer[0])) {
        VLOG_ERROR("bake", "__ensure_hostdirs: failed to create %s\n", &buffer[0]);
        return -1;
    }

    snprintf(&buffer[0], sizeof(buffer), "/chef/install/%s/%s", platform, architecture);
    if (platform_mkdir(&buffer[0])) {
        VLOG_ERROR("bake", "__ensure_hostdirs: failed to create %s\n", &buffer[0]);
        return -1;
    }

    if (platform_mkdir("/chef/toolchains")) {
        VLOG_ERROR("bake", "__ensure_hostdirs: failed to create /chef/toolchains\n");
        return -1;
    }
    return 0;
}

static char* __join_packages(struct recipe_cache_package_change* changes, int count, enum recipe_cache_change_type changeType)
{
    struct list_item* i;
    char*             buffer;
    size_t            bufferLength = 64 * 1024; // 64KB buffer for packages
    size_t            length = 0;

    if (changes == NULL || count == 0) {
        return NULL;
    }

    buffer = calloc(bufferLength, 1); 
    if (buffer == NULL) {
        return NULL;
    }

    for (int i = 0; i < count; i++) {
        struct recipe_cache_package_change* pkg = &changes[i];
        size_t                              pkgLength;
        if (pkg->type != changeType) {
            continue;
        }

        pkgLength = strlen(pkg->name);
        if ((length + pkgLength) >= bufferLength) {
            VLOG_ERROR("kitchen", "the length of package %s exceeded the total length of package names\n", pkg->name);
            free(buffer);
            return NULL;
        }

        if (buffer[0] == 0) {
            strcat(buffer, pkg->name);
            length += pkgLength;
        } else {
            strcat(buffer, " ");
            strcat(buffer, pkg->name);
            length += pkgLength + 1;
        }
    }
    if (length == 0) {
        free(buffer);
        return NULL;
    }
    return buffer;
}

static int __write_update_script(struct recipe_cache* cache)
{
    struct recipe_cache_package_change* changes;
    int                                 count;
    int                                 status;
    FILE*                               stream;
    char*                               aptpkgs;
    char*                               target;

    status = recipe_cache_calculate_package_changes(cache, &changes, &count);
    if (status) {
        VLOG_ERROR("bake", "__write_update_script: failed to calculate package differences\n");
        return status;
    }

    if (count == 0) {
        return 0;
    }

    target = strpathjoin("chef", "update.sh", NULL);
    if (target == NULL) {
        VLOG_ERROR("bake", "__write_update_script: failed to allocate memory for script path\n", target);
        recipe_cache_package_changes_destroy(changes, count);
        return -1;
    }

    stream = fopen(target, "w+");
    if (stream == NULL) {
        VLOG_ERROR("bake", "__write_update_script: failed to allocate a script stream\n");
        recipe_cache_package_changes_destroy(changes, count);
        free(target);
        return -1;
    }

    fprintf(stream, "#!/bin/bash\n\n");
    fprintf(stream, "echo \"updating container packages...\"\n");
    fprintf(stream, "apt-get -yqq update\n");

    aptpkgs = __join_packages(changes, count, RECIPE_CACHE_CHANGE_REMOVED);
    if (aptpkgs != NULL) {
        fprintf(stream, "apt-get -y -qq remove %s\n", aptpkgs);
        free(aptpkgs);
    }

    aptpkgs = __join_packages(changes, count, RECIPE_CACHE_CHANGE_ADDED);
    if (aptpkgs != NULL) {
        fprintf(stream, "apt-get -y -qq install --no-install-recommends %s\n", aptpkgs);
        free(aptpkgs);
    }

    // done with script generation
    fclose(stream);

    // chmod to executable
    status = chmod(target, S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
    if (status) {
        VLOG_ERROR("bake", "__write_update_script: failed to fixup permissions for %s\n", target);
    }
    recipe_cache_package_changes_destroy(changes, count);
    free(target);
    return 0;
}

static int __write_setup_hook_script(struct recipe* recipe)
{
    int   status;
    FILE* stream;
    char* target;

    if (recipe->environment.hooks.bash == NULL) {
        return 0;
    }

    target = strpathjoin("chef", "hook-setup.sh", NULL);
    if (target == NULL) {
        VLOG_ERROR("bake", "__write_setup_hook_script: failed to allocate memory for script path\n", target);
        return -1;
    }

    stream = fopen(target, "w+");
    if (stream == NULL) {
        VLOG_ERROR("bake", "__write_setup_hook_script: failed to allocate a script stream\n");
        free(target);
        return -1;
    }

    fprintf(stream, "#!/bin/bash\n\n");
    fputs(recipe->environment.hooks.bash, stream);
    fclose(stream);

    // chmod to executable
    status = chmod(target, S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
    if (status) {
        VLOG_ERROR("bake", "__write_setup_hook_script: failed to fixup permissions for %s\n", target);
    }
    free(target);
    return status;
}

static int __write_resources(struct __bakelib_context* context)
{
    int status;

    status = __write_update_script(context->cache);
    if (status) {
        VLOG_ERROR("bake", "__write_resources: failed to write update resource\n");
        return status;
    }

    status = __write_setup_hook_script(context->recipe);
    if (status) {
        VLOG_ERROR("bake", "__write_resources: failed to write hook resources\n");
        return status;
    }

    return status;
}

static int __setup_ingredient(struct __bakelib_context* context, struct list* ingredients, const char* hostPath)
{
    struct list_item* i;
    int               status;

    if (ingredients == NULL) {
        return 0;
    }

    list_foreach(ingredients, i) {
        struct kitchen_ingredient* kitchenIngredient = (struct kitchen_ingredient*)i;
        struct ingredient*         ingredient;

        status = ingredient_open(kitchenIngredient->path, &ingredient);
        if (status) {
            VLOG_ERROR("bake", "__setup_ingredients: failed to open %s\n", kitchenIngredient->name);
            return -1;
        }

        // Only unpack ingredients, we may encounter toolchains here.
        if (ingredient->package->type != CHEF_PACKAGE_TYPE_INGREDIENT) {
            ingredient_close(ingredient);
            continue;
        }

        status = ingredient_unpack(ingredient, hostPath, NULL, NULL);
        if (status) {
            ingredient_close(ingredient);
            VLOG_ERROR("bake", "__setup_ingredients: failed to setup %s\n", kitchenIngredient->name);
            return -1;
        }
        
        if (kitchen->pkg_manager != NULL) {
            status = kitchen->pkg_manager->make_available(kitchen->pkg_manager, ingredient);
        }
        ingredient_close(ingredient);
        if (status) {
            VLOG_ERROR("bake", "__setup_ingredients: failed to make %s available\n", kitchenIngredient->name);
            return -1;
        }
    }
    return 0;
}

static int __setup_toolchains(struct list* ingredients, const char* hostPath)
{
    struct list_item* i;
    int               status;
    char              buff[PATH_MAX];

    if (ingredients == NULL) {
        return 0;
    }

    list_foreach(ingredients, i) {
        struct kitchen_ingredient* kitchenIngredient = (struct kitchen_ingredient*)i;
        struct ingredient*         ingredient;

        status = ingredient_open(kitchenIngredient->path, &ingredient);
        if (status) {
            VLOG_ERROR("bake", "__setup_toolchains: failed to open %s\n", kitchenIngredient->name);
            return -1;
        }

        if (ingredient->package->type != CHEF_PACKAGE_TYPE_TOOLCHAIN) {
            ingredient_close(ingredient);
            continue;
        }

        snprintf(&buff[0], sizeof(buff), "%s/%s", hostPath, kitchenIngredient->name);
        if (platform_mkdir(&buff[0])) {
            VLOG_ERROR("bake", "__setup_toolchains: failed to create %s\n", &buff[0]);
            return -1;
        }

        status = ingredient_unpack(ingredient, &buff[0], NULL, NULL);
        if (status) {
            ingredient_close(ingredient);
            VLOG_ERROR("bake", "__setup_toolchains: failed to setup %s\n", kitchenIngredient->name);
            return -1;
        }
        ingredient_close(ingredient);
    }
    return 0;
}

static int __setup_ingredients(struct __bakelib_context* context)
{
    int status;

    status = __setup_ingredient(kitchen, &options->host_ingredients, kitchen->host_chroot);
    if (status) {
        return status;
    }

    status = __setup_toolchains(&options->host_ingredients, kitchen->host_build_toolchains_path);
    if (status) {
        return status;
    }

    status = __setup_ingredient(kitchen, &options->build_ingredients, kitchen->host_build_ingredients_path);
    if (status) {
        return status;
    }

    status = __setup_ingredient(kitchen, &options->runtime_ingredients, kitchen->host_install_path);
    if (status) {
        return status;
    }
    return 0;
}

static int __update_ingredients(struct __bakelib_context* context)
{
    int status;

    if (recipe_cache_key_bool(context->cache, "setup_ingredients")) {
        return 0;
    }

    VLOG_TRACE("bake", "installing project ingredients\n");
    status = __setup_ingredients(context);
    if (status) {
        VLOG_ERROR("bake", "__update_ingredients: failed to setup project ingredients\n");
        return status;
    }

    recipe_cache_transaction_begin(context->cache);
    status = recipe_cache_key_set_bool(context->cache, "setup_ingredients", 1);
    if (status) {
        VLOG_ERROR("bake", "__update_ingredients: failed to mark ingredients step as done\n");
        return status;
    }
    recipe_cache_transaction_commit(context->cache);
    return 0;
}

static int __run_setup_hook(struct __bakelib_context* context)
{
    int          status;
    char         buffer[512] = { 0 };
    unsigned int pid;

    if (options->setup_hook.bash == NULL) {
        return 0;
    }
    
    if (recipe_cache_key_bool(kitchen->recipe_cache, "setup_hook")) {
        return 0;
    }

    VLOG_TRACE("kitchen", "executing setup hook\n");
    snprintf(&buffer[0], sizeof(buffer), "/chef/hook-setup.sh");
    status = kitchen_client_spawn(
        kitchen,
        &buffer[0],
        CHEF_SPAWN_OPTIONS_WAIT,
        &pid
    );
    if (status) {
        VLOG_ERROR("kitchen", "__run_setup_hook: failed to execute setup hook\n");
        return status;
    }

    recipe_cache_transaction_begin(kitchen->recipe_cache);
    status = recipe_cache_key_set_bool(kitchen->recipe_cache, "setup_hook", 1);
    if (status) {
        VLOG_ERROR("kitchen", "__run_setup_hook: failed to mark setup hook as done\n");
        return status;
    }
    recipe_cache_transaction_commit(kitchen->recipe_cache);
    return 0;
}

static int __update_packages(struct kitchen* kitchen)
{
    struct recipe_cache_package_change* changes;
    int                                 count;
    int                                 status;
    char                                buffer[512] = { 0 };
    unsigned int                        pid;

    // this function is kinda unique, to avoid dublicating stuff the API is complex in the
    // sense that calling this with a NULL cache will just mark everything as _ADDED
    status = recipe_cache_calculate_package_changes(kitchen->recipe_cache, &changes, &count);
    if (status) {
        VLOG_ERROR("kitchen", "__update_packages: failed to calculate package differences\n");
        return status;
    }

    if (count == 0) {
        return 0;
    }

    VLOG_TRACE("kitchen", "updating build packages\n");
    snprintf(&buffer[0], sizeof(buffer), "/chef/update.sh");
    status = kitchen_client_spawn(
        kitchen,
        &buffer[0],
        CHEF_SPAWN_OPTIONS_WAIT,
        &pid
    );
    if (status) {
        VLOG_ERROR("kitchen", "__execute_script_in_container: failed to execute script\n");
        goto exit;
    }

    recipe_cache_transaction_begin(kitchen->recipe_cache);
    status = recipe_cache_commit_package_changes(kitchen->recipe_cache, changes, count);
    if (status) {
        goto exit;
    }
    recipe_cache_transaction_commit(kitchen->recipe_cache);

exit:
    recipe_cache_package_changes_destroy(changes, count);
    return status;
}

int init_main(int argc, char** argv, struct __bakelib_context* context, struct bakectl_command_options* options)
{
    int status;

    // handle individual help command
    if (argc > 1) {
        for (int i = 1; i < argc; i++) {
            if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
                __print_help();
                return 0;
            }
        }
    }

    status = __write_resources(context);
    if (status) {
        VLOG_ERROR("bake", "kitchen_setup: failed to generate resources\n");
        return status;
    }

    status = __update_ingredients(context);
    if (status) {
        VLOG_ERROR("bake", "kitchen_setup: failed to setup/refresh kitchen ingredients\n");
        return status;
    }

    status = __update_packages(context);
    if (status) {
        VLOG_ERROR("bake", "kitchen_setup: failed to install/update rootfs packages\n");
        return status;
    }

    status = __run_setup_hook(context);
    if (status) {
        VLOG_ERROR("bake", "kitchen_setup: failed to execute setup script: %s\n", strerror(errno));
        return status;
    }

    return status;
}
