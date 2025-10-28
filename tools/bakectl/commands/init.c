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
#include <chef/bake.h>
#include <chef/cache.h>
#include <chef/fridge.h>
#include <chef/ingredient.h>
#include <chef/platform.h>
#include <chef/pkgmgr.h>
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

static const char* __get_platform(void) {
    const char* platform = getenv("CHEF_TARGET_PLATFORM");
    if (platform == NULL) {
        platform = CHEF_PLATFORM_STR;
    }
    return platform;
}

static const char* __get_architecture(void) {
    const char* platform = getenv("CHEF_TARGET_ARCH");
    if (platform == NULL) {
        platform = CHEF_ARCHITECTURE_STR;
    }
    return platform;
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

    VLOG_DEBUG("bakectl", "__ensure_chef_directories()\n");


    snprintf(&buffer[0], sizeof(buffer), "/chef/build/%s/%s", platform, architecture);
    if (platform_mkdir(&buffer[0])) {
        VLOG_ERROR("bakectl", "__ensure_chef_directories: failed to create %s\n", &buffer[0]);
        return -1;
    }

    snprintf(&buffer[0], sizeof(buffer), "/chef/ingredients/%s/%s", platform, architecture);
    if (platform_mkdir(&buffer[0])) {
        VLOG_ERROR("bakectl", "__ensure_chef_directories: failed to create %s\n", &buffer[0]);
        return -1;
    }

    snprintf(&buffer[0], sizeof(buffer), "/chef/install/%s/%s", platform, architecture);
    if (platform_mkdir(&buffer[0])) {
        VLOG_ERROR("bakectl", "__ensure_chef_directories: failed to create %s\n", &buffer[0]);
        return -1;
    }

    if (platform_mkdir("/chef/toolchains")) {
        VLOG_ERROR("bakectl", "__ensure_chef_directories: failed to create /chef/toolchains\n");
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
    struct recipe_cache_package_change* changes = NULL;
    int                                 count = 0;
    int                                 status;
    FILE*                               stream;
    char*                               aptpkgs;
    char*                               target;
    VLOG_DEBUG("bakectl", "__write_update_script()\n");

    status = recipe_cache_calculate_package_changes(cache, &changes, &count);
    if (status) {
        VLOG_ERROR("bakectl", "__write_update_script: failed to calculate package differences\n");
        return status;
    }

    VLOG_DEBUG("bakectl", "__write_update_script: number of changes: %i\n", count);
    if (count == 0) {
        return 0;
    }

    target = strpathjoin("/", "chef", "update.sh", NULL);
    if (target == NULL) {
        VLOG_ERROR("bakectl", "__write_update_script: failed to allocate memory for script path\n", target);
        recipe_cache_package_changes_destroy(changes, count);
        return -1;
    }

    stream = fopen(target, "w+");
    if (stream == NULL) {
        VLOG_ERROR("bakectl", "__write_update_script: failed to open script at %s\n", target);
        recipe_cache_package_changes_destroy(changes, count);
        free(target);
        return -1;
    }

    fprintf(stream, "#!/bin/bash\n\n");
    fprintf(stream, "export DEBIAN_FRONTEND=noninteractive\n");
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
    status = platform_chmod(target, 0755);
    if (status) {
        VLOG_ERROR("bakectl", "__write_update_script: failed to fixup permissions for %s\n", target);
    }
    recipe_cache_package_changes_destroy(changes, count);
    free(target);
    return 0;
}

static int __write_resources(struct __bakelib_context* context)
{
    int status;
    VLOG_DEBUG("bakectl", "__write_resources()\n");

    status = __write_update_script(context->cache);
    if (status) {
        VLOG_ERROR("bakectl", "__write_resources: failed to write update resource\n");
        return status;
    }

    return status;
}

static int __setup_ingredient(struct __bakelib_context* context, struct list* ingredients, const char* hostPath)
{
    struct list_item* i;
    int               status;
    VLOG_DEBUG("bakectl", "__setup_ingredient()\n");

    if (ingredients == NULL) {
        return 0;
    }

    list_foreach(ingredients, i) {
        struct recipe_ingredient* ri = (struct recipe_ingredient*)i;
        struct ingredient*        ig;
        const char*               path;
        VLOG_DEBUG("bakectl", "__setup_ingredient: %s\n", ri->name);

        status = fridge_package_path(&(struct fridge_package) {
            .name = ri->name,
            .channel = ri->channel,
            .arch = context->build_architecture,
            .platform = context->build_platform
        }, &path);
        if (status) {
            VLOG_ERROR("bakectl", "__setup_ingredients: failed to find ingredient in store %s\n", ri->name);
            return -1;
        }

        status = ingredient_open(path, &ig);
        if (status) {
            VLOG_ERROR("bakectl", "__setup_ingredients: failed to open %s\n", ri->name);
            return -1;
        }

        // Only unpack ingredients, we may encounter toolchains here.
        if (ig->package->type != CHEF_PACKAGE_TYPE_INGREDIENT) {
            ingredient_close(ig);
            continue;
        }

        status = ingredient_unpack(ig, hostPath, NULL, NULL);
        if (status) {
            ingredient_close(ig);
            VLOG_ERROR("bakectl", "__setup_ingredients: failed to setup %s\n", ri->name);
            return -1;
        }
        
        if (context->pkg_manager != NULL) {
            status = context->pkg_manager->make_available(context->pkg_manager, ig);
        }
        ingredient_close(ig);
        if (status) {
            VLOG_ERROR("bakectl", "__setup_ingredients: failed to make %s available\n", ri->name);
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
    VLOG_DEBUG("bakectl", "__setup_toolchains()\n");

    if (ingredients == NULL) {
        return 0;
    }

    list_foreach(ingredients, i) {
        struct recipe_ingredient* ri = (struct recipe_ingredient*)i;
        struct ingredient*        ig;
        const char*               path;

        status = fridge_package_path(&(struct fridge_package) {
            .name = ri->name,
            .channel = ri->channel,
            .arch = CHEF_ARCHITECTURE_STR,
            .platform = CHEF_PLATFORM_STR
        }, &path);
        if (status) {
            VLOG_ERROR("bakectl", "__setup_ingredients: failed to find ingredient in store %s\n", ri->name);
            return -1;
        }

        status = ingredient_open(path, &ig);
        if (status) {
            VLOG_ERROR("bakectl", "__setup_toolchains: failed to open %s\n", ri->name);
            return -1;
        }

        if (ig->package->type != CHEF_PACKAGE_TYPE_TOOLCHAIN) {
            ingredient_close(ig);
            continue;
        }

        snprintf(&buff[0], sizeof(buff), "%s/%s", hostPath, ri->name);
        if (platform_mkdir(&buff[0])) {
            VLOG_ERROR("bakectl", "__setup_toolchains: failed to create %s\n", &buff[0]);
            return -1;
        }

        status = ingredient_unpack(ig, &buff[0], NULL, NULL);
        if (status) {
            ingredient_close(ig);
            VLOG_ERROR("bakectl", "__setup_toolchains: failed to setup %s\n", ri->name);
            return -1;
        }
        ingredient_close(ig);
    }
    return 0;
}

static int __setup_ingredients(struct __bakelib_context* context)
{
    int status;
    VLOG_DEBUG("bakectl", "__setup_ingredients()\n");

    VLOG_DEBUG("bakectl", "__setup_ingredients: setting up host ingredients\n");
    status = __setup_ingredient(context, &context->recipe->environment.host.ingredients, "/");
    if (status) {
        return status;
    }

    VLOG_DEBUG("bakectl", "__setup_ingredients: setting up host toolchains\n");
    status = __setup_toolchains(&context->recipe->environment.host.ingredients, context->build_toolchains_directory);
    if (status) {
        return status;
    }

    VLOG_DEBUG("bakectl", "__setup_ingredients: setting up build ingredients\n");
    status = __setup_ingredient(context, &context->recipe->environment.build.ingredients, context->build_ingredients_directory);
    if (status) {
        return status;
    }

    VLOG_DEBUG("bakectl", "__setup_ingredients: setting up runtime ingredients\n");
    status = __setup_ingredient(context, &context->recipe->environment.runtime.ingredients, context->install_directory);
    if (status) {
        return status;
    }
    return 0;
}

static int __update_ingredients(struct __bakelib_context* context)
{
    int status;
    VLOG_DEBUG("bakectl", "__update_ingredients()\n");

    if (recipe_cache_key_bool(context->cache, "setup_ingredients")) {
        return 0;
    }

    VLOG_TRACE("bakectl", "installing project ingredients\n");
    status = __setup_ingredients(context);
    if (status) {
        VLOG_ERROR("bakectl", "__update_ingredients: failed to setup project ingredients\n");
        return status;
    }

    recipe_cache_transaction_begin(context->cache);
    status = recipe_cache_key_set_bool(context->cache, "setup_ingredients", 1);
    if (status) {
        VLOG_ERROR("bakectl", "__update_ingredients: failed to mark ingredients step as done\n");
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

    if (context->recipe->environment.hooks.setup == NULL) {
        return 0;
    }
    
    if (recipe_cache_key_bool(context->cache, "setup_hook")) {
        return 0;
    }

    VLOG_TRACE("kitchen", "executing setup hook\n");
    status = oven_script(
        context->recipe->environment.hooks.setup,
        &(struct oven_script_options) {
            .root_dir = OVEN_SCRIPT_ROOT_DIR_SOURCE
        }
    );
    if (status) {
        VLOG_ERROR("kitchen", "__run_setup_hook: failed to execute setup hook\n");
        return status;
    }

    recipe_cache_transaction_begin(context->cache);
    status = recipe_cache_key_set_bool(context->cache, "setup_hook", 1);
    if (status) {
        VLOG_ERROR("kitchen", "__run_setup_hook: failed to mark setup hook as done\n");
        return status;
    }
    recipe_cache_transaction_commit(context->cache);
    return 0;
}

static int __update_packages(struct __bakelib_context* context)
{
    struct recipe_cache_package_change* changes = NULL;
    int                                 count = 0;
    int                                 status;
    char                                buffer[512] = { 0 };
    unsigned int                        pid;
    VLOG_DEBUG("bakectl",  "__update_packages()\n");

    // this function is kinda unique, to avoid dublicating stuff the API is complex in the
    // sense that calling this with a NULL cache will just mark everything as _ADDED
    status = recipe_cache_calculate_package_changes(context->cache, &changes, &count);
    if (status) {
        VLOG_ERROR("kitchen", "__update_packages: failed to calculate package differences\n");
        return status;
    }

    if (count == 0) {
        return 0;
    }

    VLOG_TRACE("kitchen", "updating build packages\n");
    snprintf(&buffer[0], sizeof(buffer), "/chef/update.sh");
    status = platform_spawn(
        &buffer[0],
        NULL,
        (const char* const*)context->build_environment,
        &(struct platform_spawn_options) {
            .cwd = "/chef",
        }
    );
    if (status) {
        VLOG_ERROR("kitchen", "__execute_script_in_container: failed to execute script\n");
        goto exit;
    }

    recipe_cache_transaction_begin(context->cache);
    status = recipe_cache_commit_package_changes(context->cache, changes, count);
    if (status) {
        goto exit;
    }
    recipe_cache_transaction_commit(context->cache);

exit:
    recipe_cache_package_changes_destroy(changes, count);
    return status;
}

int init_main(int argc, char** argv, struct __bakelib_context* context, struct bakectl_command_options* options)
{
    struct oven_initialize_options ovenOpts = { 0 };
    int                            status;

    // handle individual help command
    if (argc > 1) {
        for (int i = 1; i < argc; i++) {
            if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
                __print_help();
                return 0;
            }
        }
    }

    status = fridge_initialize(&(struct fridge_parameters) {
        .platform = __get_platform(),
        .architecture = __get_architecture(),
        // no backend support here
    });
    if (status) {
        VLOG_ERROR("bakectl", "failed to create initialize fridge\n");
        return status;
    }

    status = __initialize_oven_options(&ovenOpts, context);
    if (status) {
        fprintf(stderr, "bakectl: failed to allocate memory for options\n");
        fridge_cleanup();
        return status;
    }

    status = oven_initialize(&ovenOpts);
    if (status) {
        fprintf(stderr, "bakectl: failed to initialize oven: %s\n", strerror(errno));
        __destroy_oven_options(&ovenOpts);
        fridge_cleanup();
        return status;
    }
    
    status = __ensure_chef_directories();
    if (status) {
        VLOG_ERROR("bakectl", "failed to create chef directories\n");
        goto cleanup;
    }

    status = __write_resources(context);
    if (status) {
        VLOG_ERROR("bakectl", "failed to generate resources\n");
        goto cleanup;
    }

    status = __update_ingredients(context);
    if (status) {
        VLOG_ERROR("bakectl", "failed to setup/refresh kitchen ingredients\n");
        goto cleanup;
    }

    status = __update_packages(context);
    if (status) {
        VLOG_ERROR("bakectl", "failed to install/update rootfs packages\n");
        goto cleanup;
    }

    status = __run_setup_hook(context);
    if (status) {
        VLOG_ERROR("bakectl", "failed to execute setup script: %s\n", strerror(errno));
    }

cleanup:
    __destroy_oven_options(&ovenOpts);
    oven_cleanup();
    fridge_cleanup();
    return status;
}
