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

static void __cleanup_systems(int sig)
{
    (void)sig;
    printf("termination requested, cleaning up\n"); // not safe
    _Exit(0);
}

// paths
static int __ensure_chef_directories(const char* architecture, const char* platform)
{
    char buffer[1024];

    VLOG_DEBUG("kitchen", "__ensure_hostdirs()\n");


    snprintf(&buffer[0], sizeof(buffer), "/chef/build/%s/%s", platform, architecture);
    if (platform_mkdir(&buffer[0])) {
        VLOG_ERROR("kitchen", "__ensure_hostdirs: failed to create %s\n", &buffer[0]);
        return -1;
    }

    snprintf(&buffer[0], sizeof(buffer), "/chef/ingredients/%s/%s", platform, architecture);
    if (platform_mkdir(&buffer[0])) {
        VLOG_ERROR("kitchen", "__ensure_hostdirs: failed to create %s\n", &buffer[0]);
        return -1;
    }

    snprintf(&buffer[0], sizeof(buffer), "/chef/install/%s/%s", platform, architecture);
    if (platform_mkdir(&buffer[0])) {
        VLOG_ERROR("kitchen", "__ensure_hostdirs: failed to create %s\n", &buffer[0]);
        return -1;
    }

    if (platform_mkdir("/chef/toolchains")) {
        VLOG_ERROR("kitchen", "__ensure_hostdirs: failed to create /chef/toolchains\n");
        return -1;
    }

    if (platform_mkdir("/chef/fridge")) {
        VLOG_ERROR("kitchen", "__ensure_hostdirs: failed to create /chef/fridge\n");
        return -1;
    }

    if (platform_mkdir("/chef/project")) {
        VLOG_ERROR("kitchen", "__ensure_hostdirs: failed to create /chef/project\n");
        return -1;
    }
    return 0;
}

static int __write_update_script(struct recipe* recipe)
{
    struct recipe_cache_package_change* changes;
    int                                 count;
    int                                 status;
    FILE*                               stream;
    char*                               aptpkgs;
    char*                               target;

    status = recipe_cache_calculate_package_changes(kitchen->recipe_cache, &changes, &count);
    if (status) {
        VLOG_ERROR("kitchen", "__write_update_script: failed to calculate package differences\n");
        return status;
    }

    if (count == 0) {
        return 0;
    }

    target = strpathjoin("chef", "update.sh", NULL);
    if (target == NULL) {
        VLOG_ERROR("kitchen", "__write_update_script: failed to allocate memory for script path\n", target);
        recipe_cache_package_changes_destroy(changes, count);
        return -1;
    }

    stream = fopen(target, "w+");
    if (stream == NULL) {
        VLOG_ERROR("kitchen", "__write_update_script: failed to allocate a script stream\n");
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
        VLOG_ERROR("kitchen", "__write_update_script: failed to fixup permissions for %s\n", target);
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
        VLOG_ERROR("kitchen", "__write_setup_hook_script: failed to allocate memory for script path\n", target);
        return -1;
    }

    stream = fopen(target, "w+");
    if (stream == NULL) {
        VLOG_ERROR("kitchen", "__write_setup_hook_script: failed to allocate a script stream\n");
        free(target);
        return -1;
    }

    fprintf(stream, "#!/bin/bash\n\n");
    fputs(recipe->environment.hooks.bash, stream);
    fclose(stream);

    // chmod to executable
    status = chmod(target, S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
    if (status) {
        VLOG_ERROR("kitchen", "__write_setup_hook_script: failed to fixup permissions for %s\n", target);
    }
    free(target);
    return status;
}

static int __write_resources(struct kitchen* kitchen, struct kitchen_setup_options* options)
{
    int status;

    status = __write_update_script(kitchen);
    if (status) {
        VLOG_ERROR("kitchen", "__write_resources: failed to write update resource\n");
        return status;
    }

    status = __write_setup_hook_script(kitchen, options);
    if (status) {
        VLOG_ERROR("kitchen", "__write_resources: failed to write hook resources\n");
        return status;
    }

    return status;
}

static int __add_kitchen_ingredient(const char* name, const char* path, struct list* kitchenIngredients)
{
    struct kitchen_ingredient* ingredient;
    VLOG_DEBUG("bake", "__add_kitchen_ingredient(name=%s, path=%s)\n", name, path);

    ingredient = malloc(sizeof(struct kitchen_ingredient));
    if (ingredient == NULL) {
        return -1;
    }
    memset(ingredient, 0, sizeof(struct kitchen_ingredient));

    ingredient->name = name;
    ingredient->path = path;

    list_add(kitchenIngredients, &ingredient->list_header);
    return 0;
}

static int __prep_toolchains(struct list* platforms, struct list* kitchenIngredients)
{
    struct list_item* item;
    VLOG_DEBUG("bake", "__prep_toolchains()\n");

    list_foreach(platforms, item) {
        struct recipe_platform* platform = (struct recipe_platform*)item;
        int                     status;
        const char*             path;
        char*                   name;
        char*                   channel;
        char*                   version;
        if (platform->toolchain == NULL) {
            continue;
        }
        
        status = recipe_parse_platform_toolchain(platform->toolchain, &name, &channel, &version);
        if (status) {
            VLOG_ERROR("bake", "failed to parse toolchain %s for platform %s", platform->toolchain, platform->name);
            return status;
        }

        status = fridge_ensure_ingredient(&(struct fridge_ingredient) {
            .name = name,
            .channel = channel,
            .version = version,
            .arch = CHEF_ARCHITECTURE_STR,
            .platform = CHEF_PLATFORM_STR
        }, &path);
        if (status) {
            free(name);
            free(channel);
            free(version);
            VLOG_ERROR("bake", "failed to fetch ingredient %s\n", name);
            return status;
        }
        
        status = __add_kitchen_ingredient(name, path, kitchenIngredients);
        if (status) {
            free(name);
            free(channel);
            free(version);
            VLOG_ERROR("bake", "failed to mark ingredient %s\n", name);
            return status;
        }
    }
    return 0;
}

static int __prep_ingredient_list(struct list* list, const char* platform, const char* arch, struct list* kitchenIngredients)
{
    struct list_item* item;
    int               status;
    VLOG_DEBUG("bake", "__prep_ingredient_list(platform=%s, arch=%s)\n", platform, arch);

    list_foreach(list, item) {
        struct recipe_ingredient* ingredient = (struct recipe_ingredient*)item;
        const char*               path = NULL;

        status = fridge_ensure_ingredient(&(struct fridge_ingredient) {
            .name = ingredient->name,
            .channel = ingredient->channel,
            .version = ingredient->version,
            .arch = arch,
            .platform = platform
        }, &path);
        if (status) {
            VLOG_ERROR("bake", "failed to fetch ingredient %s\n", ingredient->name);
            return status;
        }
        
        status = __add_kitchen_ingredient(ingredient->name, path, kitchenIngredients);
        if (status) {
            VLOG_ERROR("bake", "failed to mark ingredient %s\n", ingredient->name);
            return status;
        }
    }
    return 0;
}

static int __ensure_ingredients(struct recipe* recipe, const char* platform, const char* arch, struct kitchen_setup_options* kitchenOptions)
{
    struct list_item* item;
    int               status;

    if (recipe->platforms.count > 0) {
        VLOG_TRACE("bake", "preparing %i platforms\n", recipe->platforms.count);
        status = __prep_toolchains(
            &recipe->platforms,
            &kitchenOptions->host_ingredients
        );
        if (status) {
            return status;
        }
    }

    if (recipe->environment.host.ingredients.count > 0) {
        VLOG_TRACE("bake", "preparing %i host ingredients\n", recipe->environment.host.ingredients.count);
        status = __prep_ingredient_list(
            &recipe->environment.host.ingredients,
            CHEF_PLATFORM_STR,
            CHEF_ARCHITECTURE_STR,
            &kitchenOptions->host_ingredients
        );
        if (status) {
            return status;
        }
    }

    if (recipe->environment.build.ingredients.count > 0) {
        VLOG_TRACE("bake", "preparing %i build ingredients\n", recipe->environment.build.ingredients.count);
        status = __prep_ingredient_list(
            &recipe->environment.build.ingredients,
            platform,
            arch,
            &kitchenOptions->build_ingredients
        );
        if (status) {
            return status;
        }
    }

    if (recipe->environment.runtime.ingredients.count > 0) {
        VLOG_TRACE("bake", "preparing %i runtime ingredients\n", recipe->environment.runtime.ingredients.count);
        status = __prep_ingredient_list(
            &recipe->environment.runtime.ingredients,
            platform,
            arch,
            &kitchenOptions->runtime_ingredients
        );
        if (status) {
            return status;
        }
    }
    return 0;
}

int init_main(int argc, char** argv, char** envp, struct bakectl_command_options* options)
{
    const char* architecture;
    const char* platform;
    int         status;

    // catch CTRL-C
    signal(SIGINT, __cleanup_systems);

    // handle individual help command
    if (argc > 1) {
        for (int i = 1; i < argc; i++) {
            if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
                __print_help();
                return 0;
            }
        }
    }

    if (options->recipe == NULL) {
        fprintf(stderr, "bakectl: --recipe must be provided\n");
        return -1;
    }

    
    architecture = getenv("CHEF_TARGET_ARCH");
    if (architecture == NULL) {
        architecture = CHEF_ARCHITECTURE_STR;
    }

    platform = getenv("CHEF_TARGET_PLATFORM");
    if (platform == NULL) {
        platform = CHEF_PLATFORM_STR;
    }

    
    // setup linux options
    setupOptions.packages = &options->recipe->environment.host.packages;

    // setup kitchen hooks
    setupOptions.setup_hook.bash = options->recipe->environment.hooks.bash;
    setupOptions.setup_hook.powershell = options->recipe->environment.hooks.powershell;
    status = kitchen_setup(&g_kitchen, &setupOptions);
    if (status) {
        vlog_content_set_status(VLOG_CONTENT_STATUS_FAILED);
        goto cleanup;
    }

    return status;
}
