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

#include <chef/kitchen.h>
#include <chef/platform.h>
#include <chef/rootfs/debootstrap.h>
#include <chef/containerv.h>
#include <chef/user.h>
#include <libingredient.h>
#include <libpkgmgr.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <vlog.h>

#include "private.h"

static int __clean_environment(const char* path)
{
    int status;
    VLOG_DEBUG("kitchen", "__clean_environment(path=%s)\n", path);
    
    // remove the root of the chroot
    // ignore if the directory doesn't exist
    status = platform_rmdir(path);
    if (status && errno != ENOENT) {
        return status;
    }
    return 0;
}

static int __ensure_hostdirs(struct kitchen* kitchen, struct containerv_user* user)
{
    VLOG_DEBUG("kitchen", "__ensure_hostdirs()\n");

    if (platform_mkdir(kitchen->host_build_path)) {
        VLOG_ERROR("kitchen", "__ensure_hostdirs: failed to create %s\n", kitchen->host_build_path);
        return -1;
    }

    if (platform_mkdir(kitchen->host_build_ingredients_path)) {
        VLOG_ERROR("kitchen", "__ensure_hostdirs: failed to create %s\n", kitchen->host_build_ingredients_path);
        return -1;
    }

    if (platform_mkdir(kitchen->host_build_toolchains_path)) {
        VLOG_ERROR("kitchen", "__ensure_hostdirs: failed to create %s\n", kitchen->host_build_toolchains_path);
        return -1;
    }

    if (platform_mkdir(kitchen->host_install_path)) {
        VLOG_ERROR("kitchen", "__ensure_hostdirs: failed to create %s\n", kitchen->host_install_path);
        return -1;
    }

    if (platform_mkdir(kitchen->host_project_path)) {
        VLOG_ERROR("kitchen", "__ensure_hostdirs: failed to create %s\n", kitchen->host_project_path);
        return -1;
    }

    if (platform_mkdir(kitchen->shared_output_path)) {
        VLOG_ERROR("kitchen", "__ensure_hostdirs: failed to create %s\n", kitchen->shared_output_path);
        return -1;
    }

    // Since we need write permissions to the build folders
    if (chown(kitchen->host_build_path, user->caller_uid, user->caller_gid)) {
        VLOG_ERROR("kitchen", "__ensure_hostdirs: failed to set permissions for %s\n", kitchen->host_build_path);
        return -1;
    }

    // Also a good idea for the output folders
    if (chown(kitchen->host_install_root, user->caller_uid, user->caller_gid)) {
        VLOG_ERROR("kitchen", "__ensure_hostdirs: failed to set permissions for %s\n", kitchen->host_install_root);
        return -1;
    }
    if (chown(kitchen->shared_output_path, user->caller_uid, user->caller_gid)) {
        VLOG_ERROR("kitchen", "__ensure_hostdirs: failed to set permissions for %s\n", kitchen->shared_output_path);
        return -1;
    }
    return 0;
}

static int __setup_rootfs(struct kitchen* kitchen, struct containerv_user* user)
{
    int status;
    VLOG_TRACE("kitchen", "initializing container rootfs\n");

    if (recipe_cache_key_bool("setup_rootfs")) {
        return 0;
    }

    status = __clean_environment(kitchen->host_kitchen_project_root);
    if (status) {
        VLOG_ERROR("kitchen", "__kitchen_install: failed to clean project environment\n");
        return status;
    }

    status = container_rootfs_setup_debootstrap(kitchen->host_chroot);
    if (status) {
        VLOG_ERROR("kitchen", "__kitchen_install: failed to setup project environment\n");
        return status;
    }

    status = __ensure_hostdirs(kitchen, user);
    if (status) {
        VLOG_ERROR("kitchen", "__kitchen_install: failed to create host directories\n");
        return status;
    }

    recipe_cache_transaction_begin();
    status = recipe_cache_key_set_bool("setup_rootfs", 1);
    if (status) {
        VLOG_ERROR("kitchen", "__kitchen_install: failed to mark install as done\n");
        return status;
    }
    recipe_cache_transaction_commit();

    return 0;
}

static int __setup_container(struct kitchen* kitchen, struct containerv_user* user)
{
    struct containerv_mount mounts[2] = { 0 };
    int                     status;
    VLOG_TRACE("kitchen", "creating build container\n");

    // two mounts
    // Installation path
    mounts[0].what = kitchen->shared_output_path;
    mounts[0].where = kitchen->host_install_root;
    mounts[0].flags = CV_MOUNT_BIND | CV_MOUNT_RECURSIVE;

    // project path
    mounts[1].what = kitchen->host_cwd;
    mounts[1].where = kitchen->host_project_path;
    mounts[1].flags = CV_MOUNT_BIND | CV_MOUNT_READONLY;
    
    // start container
    status = containerv_create(kitchen->host_chroot, CV_CAP_FILESYSTEM, mounts, 2, &kitchen->container);
    if (status) {
        VLOG_ERROR("kitchen", "__setup_container: failed to create build container\n");
        return status;
    }
    return 0;
}

static char* __join_packages(struct recipe_cache_package_change* changes, int count, enum recipe_cache_change_type changeType)
{
    struct list_item* i;
    char*             buffer;
    size_t            bufferLength = 64 * 1024; // 64KB buffer for packages
    size_t            length = 0;

    buffer = calloc(bufferLength, 1); 
    if (buffer == NULL) {
        return NULL;
    }

    if (changes == NULL || count == 0) {
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
    return buffer;
}

static int __update_packages(struct kitchen* kitchen)
{
    struct recipe_cache_package_change* changes;
    int                                 count;
    int                                 status;
    char*                               script;
    size_t                              streamLength;
    FILE*                               stream;
    char*                               aptpkgs;

    status = recipe_cache_calculate_package_changes(&changes, &count);
    if (status) {
        VLOG_ERROR("kitchen", "__update_packages: failed to calculate package differences\n");
        return status;
    }

    if (count == 0) {
        return 0;
    }

    stream = open_memstream(&script, &streamLength);
    if (stream == NULL) {
        VLOG_ERROR("kitchen", "__update_packages: failed to allocate a script stream\n");
        return -1;
    }

    fprintf(stream, "#!/bin/bash\n\n");

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

    status = containerv_script(kitchen->container, script);
    if (status) {
        VLOG_ERROR("kitchen", "__update_packages: failed to update packages\n");
        return status;
    }

    recipe_cache_transaction_begin();
    status = recipe_cache_commit_package_changes(changes, count);
    if (status) {
        return status;
    }
    recipe_cache_transaction_commit();
}

static int __setup_ingredient(struct kitchen* kitchen, struct list* ingredients, const char* hostPath)
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
            VLOG_ERROR("kitchen", "__setup_ingredients: failed to open %s\n", kitchenIngredient->name);
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
            VLOG_ERROR("kitchen", "__setup_ingredients: failed to setup %s\n", kitchenIngredient->name);
            return -1;
        }
        
        if (kitchen->pkg_manager != NULL) {
            status = kitchen->pkg_manager->make_available(kitchen->pkg_manager, ingredient);
        }
        ingredient_close(ingredient);
        if (status) {
            VLOG_ERROR("kitchen", "__setup_ingredients: failed to make %s available\n", kitchenIngredient->name);
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
            VLOG_ERROR("kitchen", "__setup_toolchains: failed to open %s\n", kitchenIngredient->name);
            return -1;
        }

        if (ingredient->package->type != CHEF_PACKAGE_TYPE_TOOLCHAIN) {
            ingredient_close(ingredient);
            continue;
        }

        snprintf(&buff[0], sizeof(buff), "%s/%s", hostPath, kitchenIngredient->name);
        if (platform_mkdir(&buff[0])) {
            VLOG_ERROR("kitchen", "__setup_toolchains: failed to create %s\n", &buff[0]);
            return -1;
        }

        status = ingredient_unpack(ingredient, &buff[0], NULL, NULL);
        if (status) {
            ingredient_close(ingredient);
            VLOG_ERROR("kitchen", "__setup_toolchains: failed to setup %s\n", kitchenIngredient->name);
            return -1;
        }
        ingredient_close(ingredient);
    }
    return 0;
}

static int __setup_ingredients(struct kitchen* kitchen, struct kitchen_setup_options* options)
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

static int __update_ingredients(struct kitchen* kitchen, struct kitchen_setup_options* options)
{
    int status;
    if (recipe_cache_key_bool("setup_ingredients")) {
        return 0;
    }

    VLOG_TRACE("kitchen", "installing project ingredients\n");
    status = __setup_ingredients(kitchen, options);
    if (status) {
        return status;
    }

    recipe_cache_transaction_begin();
    status = recipe_cache_key_set_bool("setup_ingredients", 1);
    if (status) {
        VLOG_ERROR("kitchen", "__update_ingredients: failed to mark ingredients step as done\n");
        return status;
    }
    recipe_cache_transaction_commit();
    return 0;
}

static int __run_setup_hook(struct kitchen* kitchen, struct kitchen_setup_options* options)
{
    int status;

    if (options->setup_hook.bash == NULL) {
        return 0;
    }
    if (recipe_cache_key_bool("setup_hook")) {
        return 0;
    }

    VLOG_TRACE("kitchen", "executing setup hook\n");
    status = containerv_script(kitchen->container, options->setup_hook.bash);
    if (status) {
        return status;
    }

    recipe_cache_transaction_begin();
    status = recipe_cache_key_set_bool("setup_hook", 1);
    if (status) {
        VLOG_ERROR("kitchen", "__run_setup_hook: failed to mark setup hook as done\n");
        return status;
    }
    recipe_cache_transaction_commit();
    return 0;
}

int kitchen_setup(struct kitchen* kitchen, struct kitchen_setup_options* options)
{
    struct containerv_user user;
    int                    status;
    VLOG_DEBUG("kitchen", "kitchen_setup(name=%s)\n", kitchen->recipe->project.name);
    
    if (kitchen->magic != __KITCHEN_INIT_MAGIC) {
        VLOG_ERROR("kitchen", "kitchen_setup: kitchen must be initialized before calling this\n");
        return -1;
    }

    if (containerv_user_new(&user)) {
        VLOG_ERROR("kitchen", "kitchen_setup: failed to get current user\n");
        return -1;
    }

    status = __setup_rootfs(kitchen, &user);
    if (status) {
        VLOG_ERROR("kitchen", "kitchen_setup: failed to setup rootfs of kitchen\n");
        goto cleanup;
    }

    status = __update_ingredients(kitchen, options);
    if (status) {
        VLOG_ERROR("kitchen", "kitchen_setup: failed to setup/refresh kitchen ingredients\n");
        goto cleanup;
    }

    status = __setup_container(kitchen, &user);
    if (status) {
        VLOG_ERROR("kitchen", "kitchen_setup: failed to start container\n");
        goto cleanup;
    }

    status = __run_setup_hook(kitchen, options);
    if (status) {
        VLOG_ERROR("kitchen", "kitchen_setup: failed to execute setup script: %s\n", strerror(errno));
        goto cleanup;
    }

cleanup:
    containerv_user_delete(&user);
    return status;
}

int kitchen_destroy(struct kitchen* kitchen)
{
    int status;

    if (kitchen->container) {
        status = containerv_destroy(kitchen->container);
        if (status) {
            VLOG_FATAL("kitchen", "kitchen_confined_destroy: failed to destroy container\n");
            return status;
        }
    }

    // cleanup pkg manager
    if (kitchen->pkg_manager) {
        kitchen->pkg_manager->destroy(kitchen->pkg_manager);
    }

    // cleanup paths
    free(kitchen->target_platform);
    free(kitchen->target_architecture);
    free(kitchen->host_cwd);
    free(kitchen->shared_output_path);

    free(kitchen->host_chroot);
    free(kitchen->host_kitchen_project_root);
    free(kitchen->host_build_path);
    free(kitchen->host_build_ingredients_path);
    free(kitchen->host_build_toolchains_path);
    free(kitchen->host_project_path);
    free(kitchen->host_install_root);
    free(kitchen->host_install_path);

    free(kitchen->project_root);
    free(kitchen->build_root);
    free(kitchen->build_ingredients_path);
    free(kitchen->build_toolchains_path);
    free(kitchen->install_root);
    free(kitchen->install_path);
    return 0;
}
