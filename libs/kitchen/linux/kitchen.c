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
#include <libingredient.h>
#include <libpkgmgr.h>
#include <vlog.h>

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <mntent.h>
#include <sys/mount.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "steps.h"
#include "user.h"
#include "private.h"

static int __is_mountpoint(const char* path)
{
    struct mntent* ent;
    FILE*          mtab;
    char*          resolved;
    int            found = 0;

    resolved = realpath(path, NULL);
    if (resolved == NULL) {
        return -1;
    }

    mtab = setmntent("/etc/mtab", "r");
    if (mtab == NULL) {
        free(resolved);
        return -1;
    }

    while (NULL != (ent = getmntent(mtab))) {
        if (strcmp(ent->mnt_dir, resolved) == 0) {
            found = 1;
            break;
        }
    }
    endmntent(mtab);
    free(resolved);
    return found;
}

static int __ensure_mounted_dirs(struct kitchen* kitchen, const char* projectPath)
{
    char buff[PATH_MAX];
    VLOG_DEBUG("kitchen", "__ensure_mounted_dirs()\n");
    
    if (__is_mountpoint(kitchen->host_install_root) == 0) {
        if (mount(kitchen->shared_output_path, kitchen->host_install_root, NULL, MS_BIND | MS_REC, NULL)) {
            VLOG_ERROR("kitchen", "kitchen_setup: failed to mount %s\n", kitchen->host_install_root);
            return -1;
        }
    }

    if (__is_mountpoint(kitchen->host_project_path) == 0) {
        if (mount(projectPath, kitchen->host_project_path, NULL, MS_BIND | MS_RDONLY, NULL)) {
            VLOG_ERROR("kitchen", "kitchen_setup: failed to mount %s\n", kitchen->host_project_path);
            return -1;
        }
    }
    
    snprintf(&buff[0], sizeof(buff), "%s/proc", kitchen->host_chroot);
    if (__is_mountpoint(&buff[0]) == 0) {
        if (mount("none", &buff[0], "proc", 0, NULL)) {
            VLOG_ERROR("kitchen", "kitchen_setup: failed to mount %s\n", &buff[0]);
            return -1;
        }
    }
    return 0;
}

static int __ensure_mounts_cleanup(const char* chroot)
{
    char buff[PATH_MAX];
    VLOG_DEBUG("kitchen", "__ensure_mounts_cleanup()\n");

    snprintf(&buff[0], sizeof(buff), "%s/chef/install", chroot);
    if (__is_mountpoint(&buff[0]) == 1) {
        if (umount2(&buff[0], MNT_FORCE)) {
            VLOG_ERROR("kitchen", "__ensure_mounts_cleanup: failed to unmount %s\n", &buff[0]);
            return -1;
        }
    }

    snprintf(&buff[0], sizeof(buff), "%s/chef/project", chroot);
    if (__is_mountpoint(&buff[0]) == 1) {
        if (umount2(&buff[0], MNT_FORCE)) {
            VLOG_ERROR("kitchen", "__ensure_mounts_cleanup: failed to unmount %s\n", &buff[0]);
            return -1;
        }
    }

    snprintf(&buff[0], sizeof(buff), "%s/proc", chroot);
    if (__is_mountpoint(&buff[0]) == 1) {
        if (umount(&buff[0])) {
            VLOG_ERROR("kitchen", "__ensure_mounts_cleanup: failed to unmount %s\n", &buff[0]);
            return -1;
        }
    }

    return 0;
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

static int __run_in_chroot(struct kitchen* kitchen, int (*func)(void*), void* context)
{
    int   status;
    pid_t child, wt;
    VLOG_DEBUG("kitchen", "__run_in_chroot(chroot=%s)\n", kitchen->host_chroot);

    if (!kitchen->confined) {
        // Do not do any chroot steps if running unconfined.
        VLOG_DEBUG("kitchen", "__run_in_chroot: skipping due to unconfined nature\n");
        return 0;
    }

    status = kitchen_cooking_start(kitchen);
    if (status) {
        VLOG_ERROR("kitchen", "__run_in_chroot: failed to enter environment\n");
        return status;
    }

    child = fork();
    if (child == 0) {
        // execute the script as root, as we allow hooks to run in root context
        if (setuid(geteuid())) {
            VLOG_ERROR("kitchen", "__run_in_chroot: failed to switch to root\n");
            // In this sub-process we make a clean quick exit
            _Exit(-1);
        }

        status = func(context);
        if (status) {
            VLOG_ERROR("kitchen", "__run_in_chroot: callback failed to execute\n");
        }

        // In this sub-process we make a clean quick exit
        _Exit(status);
    } else {
        wt = wait(&status);
    }

    status = kitchen_cooking_end(kitchen); 
    if (status) {
        VLOG_ERROR("kitchen", "__run_in_chroot: failed to cleanup the environment\n");
    }
    return status;
}

static int __setup_hook(void* context)
{
    struct kitchen_setup_options* options = context;

    int status = system(options->setup_hook.bash);
    if (status) {
        VLOG_ERROR("kitchen", "__setup_hook: hook failed to execute\n");
    }
    return status;
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

static void __debootstrap_output_handler(const char* line, enum platform_spawn_output_type type) 
{
    if (type == PLATFORM_SPAWN_OUTPUT_TYPE_STDOUT) {
        VLOG_TRACE("kitchen", line);
    } else {
        // clear retrace on error output
        vlog_clear_output_options(stdout, VLOG_OUTPUT_OPTION_RETRACE);
        VLOG_ERROR("kitchen", line);
    }
}

struct __package_operation_options {
    struct recipe_cache_package_change* changes;
    int                                 count;
};

static int __perform_package_operations(void* context)
{
    struct __package_operation_options* options = context;
    char                                scratchPad[PATH_MAX];
    char*                               aptpkgs;
    int                                 status;
    (void)context;

    // Skip if there are no package operations
    if (options == NULL || options->count == 0) {
        return 0;
    }

    // Start with packages to remove
    aptpkgs = __join_packages(options->changes, options->count, RECIPE_CACHE_CHANGE_REMOVED);
    if (aptpkgs != NULL) {
        snprintf(&scratchPad[0], sizeof(scratchPad), "-y -qq remove %s", aptpkgs);
        free(aptpkgs);

        VLOG_DEBUG("kitchen", "executing 'apt-get %s'\n", &scratchPad[0]);
        vlog_set_output_options(stdout, VLOG_OUTPUT_OPTION_RETRACE);
        status = platform_spawn("apt-get", &scratchPad[0], NULL, &(struct platform_spawn_options) {
            .output_handler = __debootstrap_output_handler
        });
        vlog_clear_output_options(stdout, VLOG_OUTPUT_OPTION_RETRACE);
        if (status) {
            return status;
        }
    }

    // Then packages to install
    aptpkgs = __join_packages(options->changes, options->count, RECIPE_CACHE_CHANGE_ADDED);
    if (aptpkgs != NULL) {
        snprintf(&scratchPad[0], sizeof(scratchPad), "-y -qq install --no-install-recommends %s", aptpkgs);
        free(aptpkgs);

        VLOG_DEBUG("kitchen", "executing 'apt-get %s'\n", &scratchPad[0]);
        vlog_set_output_options(stdout, VLOG_OUTPUT_OPTION_RETRACE);
        status = platform_spawn("apt-get", &scratchPad[0], NULL, &(struct platform_spawn_options) {
            .output_handler = __debootstrap_output_handler
        });
        vlog_clear_output_options(stdout, VLOG_OUTPUT_OPTION_RETRACE);
        if (status) {
            return status;
        }
    }

    return 0;
}

static int __ensure_hostdirs(struct kitchen* kitchen, struct kitchen_user* user)
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

static int __setup_rootfs(struct kitchen* kitchen, struct kitchen_user* user)
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

static int __setup_container(struct kitchen* kitchen, struct kitchen_user* user)
{
    struct containerv_mount      mounts[2] = { 0 };
    struct containerv_container* container;
    int                          status;
    VLOG_TRACE("kitchen", "creating build container\n");

    // two mounts
    // Installation path
    mounts[0].source = kitchen->shared_output_path;
    mounts[0].destination = kitchen->host_install_root;
    mounts[0].flags = CV_MOUNT_BIND | CV_MOUNT_RECURSIVE;

    // project path
    mounts[1].source = kitchen->real_project_path;
    mounts[1].destination = kitchen->host_project_path;
    mounts[1].flags = CV_MOUNT_BIND | CV_MOUNT_READONLY;
    
    // start container
    status = containerv_create(kitchen->host_chroot, CV_CAP_FILESYSTEM, mounts, 2, &container);
    if (status) {
        VLOG_ERROR("kitchen", "__setup_container: failed to create build container\n");
        return status;
    }
    return 0;
}

static int __kitchen_install(struct kitchen* kitchen, struct kitchen_user* user, struct kitchen_setup_options* options)
{
    int status;
    VLOG_TRACE("kitchen", "initializing project environment\n");

    if (recipe_cache_key_bool("setup_rootfs")) {
        return 0;
    }
    
    status = __ensure_mounts_cleanup(kitchen->host_chroot);
    if (status) {
        VLOG_ERROR("kitchen", "__kitchen_install: failed to unmount existing mounts\n");
        return status;
    }
    
    status = __clean_environment(kitchen->host_kitchen_project_root);
    if (status) {
        VLOG_ERROR("kitchen", "__kitchen_install: failed to clean project environment\n");
        return status;
    }

    VLOG_TRACE("kitchen", "initializing project environment\n");
    if (kitchen->confined) {
        status = container_rootfs_setup_debootstrap(kitchen->host_chroot);
        if (status) {
            VLOG_ERROR("kitchen", "__kitchen_install: failed to setup project environment\n");
            return status;
        }
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

static int __kitchen_refresh(struct kitchen* kitchen, struct kitchen_setup_options* options)
{
    struct __package_operation_options pkgOptions;
    int                                status;

    status = __ensure_mounted_dirs(kitchen, kitchen->real_project_path);
    if (status) {
        VLOG_ERROR("kitchen", "__kitchen_refresh: failed to create project mounts\n");
        return -1;
    }

    // install packages
    recipe_cache_transaction_begin();
    status = recipe_cache_calculate_package_changes(&pkgOptions.changes, &pkgOptions.count);
    if (status) {
        VLOG_ERROR("kitchen", "__kitchen_refresh: failed to calculate package differences\n");
        return status;
    }

    status = __run_in_chroot(kitchen, __perform_package_operations, &pkgOptions);
    if (status) {
        VLOG_ERROR("kitchen", "__kitchen_refresh: failed to initialize host packages\n");
        return -1;
    }

    status = recipe_cache_commit_package_changes(pkgOptions.changes, pkgOptions.count);
    if (status) {
        return status;
    }
    recipe_cache_transaction_commit();

    // extract os/ingredients/toolchain
    if (!recipe_cache_key_bool("setup_ingredients")) {
        VLOG_TRACE("kitchen", "installing project ingredients\n");
        status = __setup_ingredients(kitchen, options);
        if (status) {
            return -1;
        }

        recipe_cache_transaction_begin();
        status = recipe_cache_key_set_bool("setup_ingredients", 1);
        if (status) {
            VLOG_ERROR("kitchen", "__kitchen_refresh: failed to mark ingredients step as done\n");
            return status;
        }
        recipe_cache_transaction_commit();
    }

    // Run the setup hook if any
    if (options->setup_hook.bash && !recipe_cache_key_bool("setup_hook")) {
        VLOG_TRACE("kitchen", "executing setup hook\n");
        status = __run_in_chroot(kitchen, __setup_hook, options);
        if (status) {
            return -1;
        }

        recipe_cache_transaction_begin();
        status = recipe_cache_key_set_bool("setup_hook", 1);
        if (status) {
            VLOG_ERROR("kitchen", "__kitchen_refresh: failed to mark setup hook as done\n");
            return status;
        }
        recipe_cache_transaction_commit();
    }

    return 0;
}

static char* __fmt_env_option(const char* name, const char* value)
{
    char  tmp[512];
    char* result;
    snprintf(&tmp[0], sizeof(tmp), "%s=%s", name, value);
    result = strdup(&tmp[0]);
    if (result == NULL) {
        VLOG_FATAL("kitchen", "failed to allocate memory for environment option\n");
    }
    return result;
}

static char** __initialize_env(struct kitchen_user* user, const char* const* envp)
{
    char** env = calloc(6, sizeof(char*));
    if (env == NULL) {
        VLOG_FATAL("kitchen", "failed to allocate memory for environment\n");
    }

    // we are not using the parent environment yet
    (void)envp;

    env[0] = __fmt_env_option("USER", user->caller_name);
    env[1] = __fmt_env_option("USERNAME", user->caller_name);
    env[2] = __fmt_env_option("HOME", "/chef");
    env[3] = __fmt_env_option("PATH", "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin:");
    env[4] = __fmt_env_option("LD_LIBRARY_PATH", "/usr/local/lib");
    env[5] = NULL;
    return env;
}

int kitchen_setup(struct kitchen_setup_options* options, struct kitchen* kitchen)
{
    struct kitchen_user user;
    int                 status;
    VLOG_DEBUG("kitchen", "kitchen_setup(name=%s)\n", kitchen->recipe->project.name);
    
    if (kitchen->magic != __KITCHEN_INIT_MAGIC) {
        VLOG_ERROR("kitchen", "kitchen_setup: kitchen must be initialized before calling this\n");
        return -1;
    }

    if (kitchen_user_new(&user)) {
        VLOG_ERROR("kitchen", "kitchen_setup: failed to get current user\n");
        return -1;
    }

    // Now that we have the paths, we can start the oven
    // we need to ensure that the paths we provide change based on .confine status
    status = oven_initialize(&(struct oven_parameters){
        .envp = (const char* const*)__initialize_env(&user, options->envp),
        .target_architecture = kitchen->target_architecture,
        .target_platform = kitchen->target_platform,
        .paths = {
            .project_root = kitchen->confined ? kitchen->project_root : kitchen->host_project_path,
            .build_root = kitchen->confined ? kitchen->build_root : kitchen->host_build_path,
            .install_root = kitchen->confined ? kitchen->install_path : kitchen->host_install_path,
            .toolchains_root = kitchen->confined ? kitchen->build_toolchains_path : kitchen->host_build_toolchains_path,
            .build_ingredients_root = kitchen->confined ? kitchen->build_ingredients_path : kitchen->host_build_ingredients_path
        }
    });
    if (status) {
        VLOG_ERROR("kitchen", "kitchen_setup: failed to initialize oven: %s\n", strerror(errno));
        goto cleanup;
    }
    atexit(oven_cleanup);

    status = __kitchen_install(kitchen, &user, options);
    if (status) {
        VLOG_ERROR("kitchen", "kitchen_setup: failed to initialize environment: %s\n", strerror(errno));
        goto cleanup;
    }

    status = __kitchen_refresh(kitchen, options);
    if (status) {
        VLOG_ERROR("kitchen", "kitchen_setup: failed to refresh kitchen: %s\n", strerror(errno));
        goto cleanup;
    }

cleanup:
    kitchen_user_delete(&user);
    return status;
}

static int __recipe_clean(const char* uuid)
{
    char root[PATH_MAX] = { 0 };
    int  status;

    status = __get_kitchen_root(&root[0], sizeof(root) - 1, uuid);
    if (status) {
        VLOG_ERROR("kitchen", "__recipe_clean: failed to resolve root directory\n");
        return -1;
    }

    strcat(&root[0], "/ns");
    status = __ensure_mounts_cleanup(&root[0]);
    if (status) {
        VLOG_ERROR("kitchen", "__recipe_clean: failed to cleanup mounts\n");
        return -1;
    }

    // remove /ns again to clean up entire folder
    root[strlen(&root[0]) - 3] = 0;

    status = __clean_environment(&root[0]);
    if (status) {
        if (errno != ENOENT) {
            VLOG_ERROR("kitchen", "__recipe_clean: failed: %s\n", strerror(errno));
            return -1;
        }
    }
    return 0;
}

int kitchen_purge(struct kitchen_purge_options* options)
{
    struct kitchen_user user;
    struct list         recipes;
    struct list_item*   i;
    int                 status;
    char                root[PATH_MAX] = { 0 };
    VLOG_DEBUG("kitchen", "kitchen_purge()\n");

    status = __get_kitchen_root(&root[0], sizeof(root) - 1, NULL);
    if (status) {
        VLOG_ERROR("kitchen", "kitchen_purge: failed to resolve root directory\n");
        return -1;
    }

    status = kitchen_user_new(&user);
    if (status) {
        VLOG_ERROR("kitchen", "kitchen_purge: failed to get current user\n");
        return -1;
    }

    list_init(&recipes);
    status = platform_getfiles(&root[0], 0, &recipes);
    if (status) {
        // ignore this error, just means there is no cleanup to be done
        if (errno != ENOENT) {
            VLOG_ERROR("kitchen", "kitchen_purge: failed to get current recipes\n");
        }
        goto cleanup;
    }

    list_foreach (&recipes, i) {
        struct platform_file_entry* entry = (struct platform_file_entry*)i;
        status = __recipe_clean(entry->name);
        if (status) {
            VLOG_ERROR("kitchen", "kitchen_purge: failed to remove data for %s\n", entry->name);
            goto cleanup;
        }

        recipe_cache_transaction_begin();
        status = recipe_cache_clear_for(entry->name);
        if (status) {
            VLOG_ERROR("kitchen", "kitchen_purge: failed to clean cache for %s\n", entry->name);
            goto cleanup;
        }
        recipe_cache_transaction_commit();
    }

cleanup:
    kitchen_user_delete(&user);
    platform_getfiles_destroy(&recipes);
    return 0;
}

int kitchen_recipe_purge(struct kitchen* kitchen, struct kitchen_recipe_purge_options* options)
{
    struct kitchen_user user;
    int                 status;

    VLOG_DEBUG("kitchen", "kitchen_recipe_purge()\n");

    if (kitchen_user_new(&user)) {
        VLOG_ERROR("kitchen", "kitchen_recipe_purge: failed to get current user\n");
        return -1;
    }

    status = __recipe_clean(recipe_cache_uuid());
    if (status) {
        VLOG_ERROR("kitchen", "kitchen_recipe_purge: failed to cleanup mounts\n");
    }

    kitchen_user_delete(&user);
    return status;
}
