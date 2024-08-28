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

#include <chef/environment.h>
#include <chef/ingredient.h>
#include <chef/kitchen.h>
#include <chef/platform.h>
#include <chef/rootfs/debootstrap.h>
#include <chef/containerv.h>
#include <chef/containerv-user-linux.h>
#include <libgen.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <vlog.h>

#include "../pkgmgrs/libpkgmgr.h"
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

static int __ensure_kitchen_dirs(struct kitchen* kitchen)
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
    return 0;
}

static int __ensure_hostdirs(struct kitchen* kitchen)
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
    return 0;
}

static int __install_bakectl(struct kitchen* kitchen)
{
    char  buffer[PATH_MAX] = { 0 };
    char* path;
    char* resolved;
    char* target;
    int   status;

    status = readlink("/proc/self/exe", &buffer[0], PATH_MAX); 
    if (status < 0) {
        VLOG_ERROR("kitchen", "__install_bakectl: failed to read /proc/self/exe\n");
        return status;
    }

    // get the directory part, and remember that this modifies
    // our buffer.
    path = dirname(&buffer[0]);

    // overwrite the binary part
    strcat(&buffer[0], "/../lib/chef/bakectl");

    resolved = realpath(&buffer[0], NULL);
    if (resolved == NULL) {
        VLOG_ERROR("kitchen", "__install_bakectl: failed to resolve path %s\n", &buffer[0]);
        return -1;
    }

    target = strpathjoin(kitchen->host_chroot, kitchen->bakectl_path, NULL);
    if (target == NULL) {
        free(resolved);
        VLOG_ERROR("kitchen", "__install_bakectl: failed to allocate memory for target\n");
        return -1;
    }

    // Copyfile does a dummy copy by creating a new file and copying contents. This
    // unfortunately does not transfer permissions.
    status = platform_copyfile(resolved, target);
    if (status) {
        VLOG_ERROR("kitchen", "__install_bakectl: failed to install %s\n", target);
        goto cleanup;
    }

    // Restore executable permissions
    status = chmod(target, S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
    if (status) {
        VLOG_ERROR("kitchen", "__install_bakectl: failed to fixup permissions for %s\n", target);
    }

cleanup:
    free(resolved);
    free(target);
    return status;
}

static int __setup_rootfs(struct kitchen* kitchen)
{
    int status;
    VLOG_TRACE("kitchen", "installing rootfs\n");

    if (recipe_cache_key_bool("setup_rootfs")) {
        return 0;
    }

    status = __clean_environment(kitchen->host_kitchen_project_data_root);
    if (status) {
        VLOG_ERROR("kitchen", "__kitchen_install: failed to clean project environment\n");
        return status;
    }

    status = container_rootfs_setup_debootstrap(kitchen->host_chroot);
    if (status) {
        VLOG_ERROR("kitchen", "__kitchen_install: failed to setup project environment\n");
        return status;
    }

    status = __ensure_hostdirs(kitchen);
    if (status) {
        VLOG_ERROR("kitchen", "__kitchen_install: failed to create host directories\n");
        return status;
    }

    status = __install_bakectl(kitchen);
    if (status) {
        VLOG_ERROR("kitchen", "__kitchen_install: failed to install controller\n");
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

static int __setup_container(struct kitchen* kitchen)
{
    struct containerv_mount    mounts[1] = { 0 };
    struct containerv_options* options;
    int                        status;
    VLOG_TRACE("kitchen", "creating build container\n");

    options = containerv_options_new();
    if (options == NULL) {
        VLOG_ERROR("kitchen", "__setup_container: failed to allocate memory for container options\n");
        return -1;
    }

    // project path
    mounts[0].what = kitchen->host_cwd;
    mounts[0].where = kitchen->project_root;
    mounts[0].flags = CV_MOUNT_BIND | CV_MOUNT_READONLY;

    // we want as many caps as makes sense
    containerv_options_set_caps(options, 
        CV_CAP_FILESYSTEM |
        CV_CAP_PROCESS_CONTROL |
        CV_CAP_IPC
    );
    containerv_options_set_mounts(options, mounts, 1);

    // start container
    status = containerv_create(kitchen->host_chroot, options, &kitchen->container);
    containerv_options_delete(options);
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

static int __update_packages(struct kitchen* kitchen)
{
    struct recipe_cache_package_change* changes;
    int                                 count;
    int                                 status;
    char                                buffer[512] = { 0 };

    status = recipe_cache_calculate_package_changes(&changes, &count);
    if (status) {
        VLOG_ERROR("kitchen", "__update_packages: failed to calculate package differences\n");
        return status;
    }

    if (count == 0) {
        return 0;
    }

    VLOG_TRACE("kitchen", "updating build packages\n");
    snprintf(&buffer[0], sizeof(buffer), "/chef/update.sh");
    status = containerv_spawn(
        kitchen->container,
        &buffer[0],
        &(struct containerv_spawn_options) {
            .arguments = NULL,
            .environment = (const char* const*)kitchen->base_environment,
            .flags = CV_SPAWN_WAIT
        },
        NULL
    );
    if (status) {
        VLOG_ERROR("kitchen", "__execute_script_in_container: failed to execute script\n");
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

static int __update_build_envs(struct kitchen* kitchen, struct list* ingredients)
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

        if (environment_append_keyv(kitchen->base_environment, "CHEF_BUILD_PATH", ingredient->options->bin_dirs, ";") |
            environment_append_keyv(kitchen->base_environment, "CHEF_BUILD_INCLUDE", ingredient->options->inc_dirs, ";") |
            environment_append_keyv(kitchen->base_environment, "CHEF_BUILD_LIBS", ingredient->options->lib_dirs, ";") |
            environment_append_keyv(kitchen->base_environment, "CHEF_BUILD_CCFLAGS", ingredient->options->compiler_flags, ";") |
            environment_append_keyv(kitchen->base_environment, "CHEF_BUILD_LDFLAGS", ingredient->options->linker_flags, ";")) {
            VLOG_ERROR("kitchen", "__setup_ingredients: failed to build environment values\n");
            return -1;
        }

        ingredient_close(ingredient);
        if (status) {
            VLOG_ERROR("kitchen", "__setup_ingredients: failed to make %s available\n", kitchenIngredient->name);
            return -1;
        }
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
        VLOG_ERROR("kitchen", "__update_ingredients: failed to setup project ingredients\n");
        return status;
    }

    status = __update_build_envs(kitchen, &options->build_ingredients);
    if (status) {
        VLOG_ERROR("kitchen", "__update_ingredients: failed to update build environments\n");
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
    int  status;
    char buffer[512] = { 0 };

    if (options->setup_hook.bash == NULL) {
        return 0;
    }
    if (recipe_cache_key_bool("setup_hook")) {
        return 0;
    }

    VLOG_TRACE("kitchen", "executing setup hook\n");
    snprintf(&buffer[0], sizeof(buffer), "/chef/hook-setup.sh");
    status = containerv_spawn(
        kitchen->container,
        &buffer[0],
        &(struct containerv_spawn_options) {
            .arguments = NULL,
            .environment = (const char* const*)kitchen->base_environment,
            .flags = CV_SPAWN_WAIT
        },
        NULL
    );
    if (status) {
        VLOG_ERROR("kitchen", "__run_setup_hook: failed to execute setup hook\n");
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

static int __write_update_script(struct kitchen* kitchen)
{
    struct recipe_cache_package_change* changes;
    int                                 count;
    int                                 status;
    FILE*                               stream;
    char*                               aptpkgs;
    char*                               target;

    status = recipe_cache_calculate_package_changes(&changes, &count);
    if (status) {
        VLOG_ERROR("kitchen", "__write_update_script: failed to calculate package differences\n");
        return status;
    }

    if (count == 0) {
        return 0;
    }

    target = strpathjoin(kitchen->host_chroot, "chef", "update.sh", NULL);
    if (target == NULL) {
        VLOG_ERROR("kitchen", "__write_update_script: failed to allocate memory for script path\n", target);
        return -1;
    }

    stream = fopen(target, "w+");
    if (stream == NULL) {
        VLOG_ERROR("kitchen", "__write_update_script: failed to allocate a script stream\n");
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
        free(target);
        return -1;
    }
    free(target);
    return 0;
}

static int __write_setup_hook_script(struct kitchen* kitchen, struct kitchen_setup_options* options)
{
    int   status;
    FILE* stream;
    char* target;

    if (options->setup_hook.bash == NULL) {
        return 0;
    }

    target = strpathjoin(kitchen->host_chroot, "chef", "hook-setup.sh", NULL);
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
    fputs(options->setup_hook.bash, stream);
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

int kitchen_setup(struct kitchen* kitchen, struct kitchen_setup_options* options)
{
    int status;
    VLOG_DEBUG("kitchen", "kitchen_setup(name=%s)\n", kitchen->recipe->project.name);
    
    if (kitchen->magic != __KITCHEN_INIT_MAGIC) {
        VLOG_ERROR("kitchen", "kitchen_setup: kitchen must be initialized before calling this\n");
        return -1;
    }

    status = __setup_rootfs(kitchen);
    if (status) {
        VLOG_ERROR("kitchen", "kitchen_setup: failed to setup rootfs of kitchen\n");
        return status;
    }

    status = __write_resources(kitchen, options);
    if (status) {
        VLOG_ERROR("kitchen", "kitchen_setup: failed to generate resources\n");
        return status;
    }

    status = __update_ingredients(kitchen, options);
    if (status) {
        VLOG_ERROR("kitchen", "kitchen_setup: failed to setup/refresh kitchen ingredients\n");
        return status;
    }

    status = __setup_container(kitchen);
    if (status) {
        VLOG_ERROR("kitchen", "kitchen_setup: failed to start container\n");
        return status;
    }

    status = __update_packages(kitchen);
    if (status) {
        VLOG_ERROR("kitchen", "kitchen_setup: failed to install/update rootfs packages\n");
        return status;
    }

    status = __run_setup_hook(kitchen, options);
    if (status) {
        VLOG_ERROR("kitchen", "kitchen_setup: failed to execute setup script: %s\n", strerror(errno));
        return status;
    }
    return status;
}
