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
    VLOG_DEBUG("kitchen", "__ensure_mounted_dirs()\n");
    
    if (mount(kitchen->shared_output_path, kitchen->host_install_root, NULL, MS_BIND | MS_SHARED, NULL)) {
        VLOG_ERROR("kitchen", "kitchen_setup: failed to mount %s\n", kitchen->host_install_root);
        return -1;
    }

    if (mount(projectPath, kitchen->host_project_path, NULL, MS_BIND | MS_PRIVATE | MS_RDONLY, NULL)) {
        VLOG_ERROR("kitchen", "kitchen_setup: failed to mount %s\n", kitchen->host_project_path);
        return -1;
    }
    return 0;
}

static void __ensure_mounts_cleanup(const char* kitchenRoot, const char* name)
{
    char buff[2048];
    VLOG_DEBUG("kitchen", "__ensure_mounts_cleanup()\n");

    snprintf(&buff[0], sizeof(buff), "%s/%s/chef/install", kitchenRoot, name);
    if (umount(&buff[0])) {
        VLOG_DEBUG("kitchen", "__ensure_mounts_cleanup: failed to unmount %s\n", &buff[0]);
    }

    snprintf(&buff[0], sizeof(buff), "%s/%s/chef/project", kitchenRoot, name);
    if (umount(&buff[0])) {
        VLOG_DEBUG("kitchen", "__ensure_mounts_cleanup: failed to unmount %s\n", &buff[0]);
    }
}

static int __recreate_dir(const char* path)
{
    int status;

    status = platform_rmdir(path);
    if (status) {
        if (errno != ENOENT) {
            VLOG_ERROR("kitchen", "__recreate_dir: failed to remove directory: %s\n", strerror(errno));
            return -1;
        }
    }

    status = platform_mkdir(path);
    if (status) {
        VLOG_ERROR("kitchen", "__recreate_dir: failed to create directory: %s\n", strerror(errno));
        return -1;
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
    char              buff[512];

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

static int __setup_hook(struct kitchen* kitchen, struct kitchen_setup_options* options)
{
    int   status;
    pid_t child, wt;
    VLOG_DEBUG("kitchen", "__setup_hook(hook=%s)\n", options->setup_hook.bash);

    status = kitchen_cooking_start(kitchen);
    if (status) {
        VLOG_ERROR("kitchen", "__setup_hook: failed to enter environment\n");
        return status;
    }

    child = fork();
    if (child == 0) {
        // execute the script as root, as we allow hooks to run in root context
        if (setuid(geteuid())) {
            VLOG_ERROR("kitchen", "__setup_hook: failed to switch to root\n");
            // In this sub-process we make a clean quick exit
            _Exit(-1);
        }

        status = system(options->setup_hook.bash);
        if (status) {
            VLOG_ERROR("kitchen", "__setup_hook: hook failed to execute\n");
        }

        // In this sub-process we make a clean quick exit
        _Exit(status);
    } else {
        wt = wait(&status);
    }

    status = kitchen_cooking_end(kitchen); 
    if (status) {
        VLOG_ERROR("kitchen", "__setup_hook: failed to cleanup the environment\n");
    }
    return status;
}

static unsigned int __hash(unsigned int hash, const char* data, size_t length)
{
    for (unsigned int i = 0; i < length; i++) {
        unsigned char c = (unsigned char)data[i];
        hash = ((hash << 5) + hash) + c;
    }
    return hash;
}

static unsigned int __hash_ingredients(struct list* ingredients, unsigned int seed)
{
    unsigned int      hash = seed;
    struct list_item* i;

    list_foreach(ingredients, i) {
        struct kitchen_ingredient* ing = (struct kitchen_ingredient*)i;
        hash = __hash(hash, ing->name, strlen(ing->name));
    }
    return hash;
}

static unsigned int __hash_packages(struct list* packages, unsigned int seed)
{
    unsigned int      hash = seed;
    struct list_item* i;

    list_foreach(packages, i) {
        struct oven_value_item* ing = (struct oven_value_item*)i;
        hash = __hash(hash, ing->value, strlen(ing->value));
    }
    return hash;
}

// hash of ingredients and imports
static unsigned int __setup_hash(struct kitchen_setup_options* options)
{
    unsigned int hash = 5381;

    // hash name
    hash = __hash(hash, options->name, strlen(options->name));

    // hash ingredients
    hash = __hash_ingredients(&options->host_ingredients, hash);
    hash = __hash_ingredients(&options->build_ingredients, hash);
    hash = __hash_ingredients(&options->runtime_ingredients, hash);

    // hash packages
    hash = __hash_packages(options->packages, hash);

    return hash;
}

static unsigned int __read_hash(const char* path)
{
    char  buff[128];
    FILE* hashFile;
    long  size;
    char* end = NULL;
    VLOG_DEBUG("kitchen", "__read_hash()\n");

    hashFile = fopen(path, "r");
    if (hashFile == NULL) {
        VLOG_DEBUG("kitchen", "__read_hash: no hash file\n");
        return 0;
    }

    fseek(hashFile, 0, SEEK_END);
    size = ftell(hashFile);
    rewind(hashFile);

    if (size >= sizeof(buff)) {
        VLOG_ERROR("kitchen", "__read_hash: the hash file is invalid\n");
        fclose(hashFile);
        return 0;
    }
    if (fread(&buff[0], 1, size, hashFile) < size) {
        VLOG_ERROR("kitchen", "__read_hash: failed to read hash file\n");
        fclose(hashFile);
        return 0;
    }
    
    fclose(hashFile);
    return (unsigned int)strtoul(&buff[0], &end, 10);
}

static int __write_hash(const char* path, unsigned int hash)
{
    FILE* hashFile;
    VLOG_DEBUG("kitchen", "__write_hash(hash=%d)\n", hash);

    hashFile = fopen(path, "w");
    if (hashFile == NULL) {
        VLOG_ERROR("kitchen", "__write_hash: failed to write hashfile: %s\n", path);
        return 0;
    }
    fprintf(hashFile, "%u", hash);
    fclose(hashFile);
    return 0;
}

static int __should_skip_setup(struct kitchen* kitchen)
{
    unsigned int existingHash = __read_hash(kitchen->host_hash_file);
    return kitchen->hash == existingHash;
}

static struct pkgmngr* __setup_pkg_environment(struct kitchen_setup_options* options, const char* chroot)
{
    static struct {
        const char* environment;
        struct pkgmngr* (*create)(struct pkgmngr_options*);
    } systems[] = {
        { "pkg-config", pkgmngr_pkgconfig_new },
        { NULL, NULL }
    };
    const char* env = options->pkg_environment;

    // default to pkg-config
    if (env == NULL) {
        env = "pkg-config";
    }

    for (int i = 0; systems[i].environment != NULL; i++) {
        if (strcmp(env, systems[i].environment) == 0) {
            VLOG_TRACE("kitchen", "initializing %s environment\n", env);
            return systems[i].create(&(struct pkgmngr_options) {
                .root = chroot, 
                .target_platform = options->target_platform,
                .target_architecture = options->target_architecture
            });
        }
    }
    return NULL;
}

// <root>/.kitchen/output
// <root>/.kitchen/<recipe>/bin
// <root>/.kitchen/<recipe>/lib
// <root>/.kitchen/<recipe>/share
// <root>/.kitchen/<recipe>/usr/...
// <root>/.kitchen/<recipe>/chef/build
// <root>/.kitchen/<recipe>/chef/ingredients
// <root>/.kitchen/<recipe>/chef/toolchains
// <root>/.kitchen/<recipe>/chef/install => <root>/.kitchen/output
// <root>/.kitchen/<recipe>/chef/project => <root>
static int __kitchen_construct(struct kitchen_setup_options* options, struct kitchen* kitchen)
{
    char buff[2048];
    VLOG_DEBUG("kitchen", "__kitchen_construct(name=%s)\n", options->name);

    memset(kitchen, 0, sizeof(struct kitchen));
    kitchen->target_platform = strdup(options->target_platform);
    kitchen->target_architecture = strdup(options->target_architecture);
    kitchen->real_project_path = strdup(options->project_path);
    kitchen->confined = options->confined;
    kitchen->hash = __setup_hash(options);

    snprintf(&buff[0], sizeof(buff), "%s/.kitchen/output", options->project_path);
    kitchen->shared_output_path = strdup(&buff[0]);

    // Format external chroot paths that are arch/platform agnostic
    snprintf(&buff[0], sizeof(buff), "%s/.kitchen/%s", options->project_path, options->name);
    kitchen->host_chroot = strdup(&buff[0]);
    kitchen->pkg_manager = __setup_pkg_environment(options, &buff[0]);

    snprintf(&buff[0], sizeof(buff), "%s/.kitchen/%s/chef/project", options->project_path, options->name);
    kitchen->host_project_path = strdup(&buff[0]);

    snprintf(&buff[0], sizeof(buff), "%s/.kitchen/%s/chef/install",
        options->project_path, options->name
    );
    kitchen->host_install_root = strdup(&buff[0]);

    snprintf(&buff[0], sizeof(buff), "%s/.kitchen/%s/chef/toolchains", options->project_path, options->name);
    kitchen->host_build_toolchains_path = strdup(&buff[0]);

    snprintf(&buff[0], sizeof(buff), "%s/.kitchen/%s/chef/.hash", options->project_path, options->name);
    kitchen->host_hash_file = strdup(&buff[0]);

    // Build/ingredients/install/checkpoint paths are different for each target
    snprintf(&buff[0], sizeof(buff), "%s/.kitchen/%s/chef/build/%s/%s",
        options->project_path, options->name,
        options->target_platform, options->target_architecture
    );
    kitchen->host_build_path = strdup(&buff[0]);

    snprintf(&buff[0], sizeof(buff), "%s/.kitchen/%s/chef/ingredients/%s/%s",
        options->project_path, options->name,
        options->target_platform, options->target_architecture
    );
    kitchen->host_build_ingredients_path = strdup(&buff[0]);

    snprintf(&buff[0], sizeof(buff), "%s/.kitchen/%s/chef/install/%s/%s",
        options->project_path, options->name,
        options->target_platform, options->target_architecture
    );
    kitchen->host_install_path = strdup(&buff[0]);

    snprintf(&buff[0], sizeof(buff), "%s/.kitchen/%s/chef/data/%s/%s",
        options->project_path, options->name,
        options->target_platform, options->target_architecture
    );
    kitchen->host_checkpoint_path = strdup(&buff[0]);

    // Format the internal chroot paths, again, we have paths that are shared between
    // platforms and archs
    kitchen->project_root = strdup("/chef/project");
    kitchen->build_toolchains_path = strdup("/chef/toolchains");
    kitchen->install_root = strdup("/chef/install");

    // And those that are not
    snprintf(&buff[0], sizeof(buff), "/chef/data/%s/%s",
        options->target_platform, options->target_architecture
    );
    kitchen->checkpoint_root = strdup(&buff[0]);

    snprintf(&buff[0], sizeof(buff), "/chef/build/%s/%s",
        options->target_platform, options->target_architecture
    );
    kitchen->build_root = strdup(&buff[0]);

    snprintf(&buff[0], sizeof(buff), "/chef/ingredients/%s/%s",
        options->target_platform, options->target_architecture
    );
    kitchen->build_ingredients_path = strdup(&buff[0]);

    snprintf(&buff[0], sizeof(buff), "/chef/install/%s/%s",
        options->target_platform, options->target_architecture
    );
    kitchen->install_path = strdup(&buff[0]);
    return 0;
}

static char* __build_include_string(struct list* packages)
{
    struct list_item* i;
    char*             buffer;
    size_t            bufferLength = 64 * 1024; // 64KB buffer for packages
    size_t            length = 0;

    buffer = calloc(bufferLength, 1); 
    if (buffer == NULL) {
        return NULL;
    }

    if (packages == NULL || packages->count == 0) {
        return NULL;
    }

    list_foreach(packages, i) {
        struct oven_value_item* pkg = (struct oven_value_item*)i;
        size_t                  pkgLength = strlen(pkg->value);
        if ((length + pkgLength) >= bufferLength) {
            VLOG_ERROR("kitchen", "the length of package %s exceeded the total length of package names\n", pkg->value);
            free(buffer);
            return NULL;
        }

        if (buffer[0] == 0) {
            strcpy(buffer, "--include=");
            strcat(buffer, pkg->value);
            length += pkgLength + 10;
        } else {
            strcat(buffer, ",");
            strcat(buffer, pkg->value);
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

static int __setup_environment(struct list* packages, int confined, const char* path)
{
    char  scratchPad[512];
    char* includes;
    int   status;
    pid_t child, wt;
    VLOG_DEBUG("kitchen", "__setup_environment(confined=%i, path=%s)\n", confined, path);

    // If we are running unconfined we don't setup environment
    if (!confined) {
        return 0;
    }

    status = platform_spawn("debootstrap", "--version", NULL, &(struct platform_spawn_options) {
        .output_handler = __debootstrap_output_handler
    });
    if (status) {
        VLOG_ERROR("kitchen", "__setup_environment: \"debootstrap\" package must be installed\n");
        return status;
    }

    includes = __build_include_string(packages);
    if (includes != NULL) {
        snprintf(&scratchPad[0], sizeof(scratchPad), "--variant=minbase %s stable %s http://deb.debian.org/debian/", includes, path);
        free(includes);
    } else {
        snprintf(&scratchPad[0], sizeof(scratchPad), "--variant=minbase stable %s http://deb.debian.org/debian/", path);
    }

    child = fork();
    if (child == 0) {
        // debootstrap must run under the root user, so lets make sure we've switched
        // to root as the real user.
        if (setuid(geteuid())) {
            VLOG_ERROR("kitchen", "__setup_environment: failed to switch to root\n");
            _Exit(-1);
        }

        vlog_set_output_options(stdout, VLOG_OUTPUT_OPTION_RETRACE);
        status = platform_spawn("debootstrap", &scratchPad[0], NULL, &(struct platform_spawn_options) {
            .output_handler = __debootstrap_output_handler
        });
        vlog_clear_output_options(stdout, VLOG_OUTPUT_OPTION_RETRACE);
        if (status) {
            VLOG_ERROR("kitchen", "__setup_environment: \"debootstrap\" failed: %i\n", status);
            VLOG_ERROR("kitchen", "see %s/debootstrap/debootstrap.log for details\n", path);
            _Exit(-1);
        }
        _Exit(0);
    } else {
        wt = wait(&status);
    }
    return status;
}

static int __ensure_hostdirs(struct kitchen* kitchen, struct kitchen_user* user)
{
    VLOG_DEBUG("kitchen", "__ensure_hostdirs()\n");

    if (platform_mkdir(kitchen->host_build_path)) {
        VLOG_ERROR("kitchen", "__ensure_hostdirs: failed to create %s\n", kitchen->host_build_path);
        return -1;
    }

    if (platform_mkdir(kitchen->host_checkpoint_path)) {
        VLOG_ERROR("kitchen", "__ensure_hostdirs: failed to create %s\n", kitchen->host_checkpoint_path);
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

    // And the data path
    if (chown(kitchen->host_checkpoint_path, user->caller_uid, user->caller_gid)) {
        VLOG_ERROR("kitchen", "__ensure_hostdirs: failed to set permissions for %s\n", kitchen->host_checkpoint_path);
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

int kitchen_setup(struct kitchen_setup_options* options, struct kitchen* kitchen)
{
    struct kitchen_user user;
    int                status;

    VLOG_DEBUG("kitchen", "kitchen_setup(name=%s)\n", options->name);

    // Start out by constructing the kitchen. The reason for this is we need all the
    // paths calculated for pretty much all other operations.
    if (__kitchen_construct(options, kitchen)) {
        return -1;
    }

    // Now that we have the paths, we can start the oven
    // we need to ensure that the paths we provide change based on .confine status
    status = oven_initialize(&(struct oven_parameters){
        .envp = options->envp,
        .target_architecture = options->target_architecture,
        .target_platform = options->target_platform,
        .paths = {
            .project_root = options->confined ? kitchen->project_root : kitchen->host_project_path,
            .build_root = options->confined ? kitchen->build_root : kitchen->host_build_path,
            .install_root = options->confined ? kitchen->install_path : kitchen->host_install_path,
            .checkpoint_root = options->confined ? kitchen->checkpoint_root : kitchen->host_checkpoint_path,
            .toolchains_root = options->confined ? kitchen->build_toolchains_path : kitchen->host_build_toolchains_path,
        }
    });
    if (status) {
        VLOG_ERROR("kitchen", "failed to initialize oven: %s\n", strerror(errno));
        return -1;
    }
    atexit(oven_cleanup);

    if (__should_skip_setup(kitchen)) {
        // ensure dirs are mounted still, they only persist till reboot
        status = __is_mountpoint(kitchen->host_install_root);
        if (status < 0) {
            VLOG_ERROR("kitchen", "failed to determine whether or not directories are mounted\n");
            return status;
        }

        // __is_mountpoint returns 0 if dir was not a mount
        if (status == 0) {
            status = __ensure_mounted_dirs(kitchen, options->project_path);
            if (status) {
                VLOG_ERROR("kitchen", "kitchen_setup: failed to create project mounts\n");
                return status;
            }
        }
        return 0;
    }

    if (kitchen_user_new(&user)) {
        VLOG_ERROR("kitchen", "kitchen_setup: failed to get current user\n");
        return -1;
    }

    VLOG_TRACE("kitchen", "cleaning project environment\n");
    __ensure_mounts_cleanup(kitchen->host_chroot, options->name);
    status = __clean_environment(kitchen->host_chroot);
    if (status) {
        VLOG_ERROR("kitchen", "kitchen_setup: failed to clean project environment\n");
        goto cleanup;
    }

    VLOG_TRACE("kitchen", "initializing project environment\n");
    status = __setup_environment(options->packages, options->confined, kitchen->host_chroot);
    if (status) {
        VLOG_ERROR("kitchen", "kitchen_setup: failed to setup project environment\n");
        goto cleanup;
    }

    status = __ensure_hostdirs(kitchen, &user);
    if (status) {
        VLOG_ERROR("kitchen", "kitchen_setup: failed to create host directories\n");
        goto cleanup;
    }

    status = __ensure_mounted_dirs(kitchen, options->project_path);
    if (status) {
        VLOG_ERROR("kitchen", "kitchen_setup: failed to create project mounts\n");
        goto cleanup;
    }

    // extract os/ingredients/toolchain
    VLOG_TRACE("kitchen", "installing project ingredients\n");
    status = __setup_ingredients(kitchen, options);
    if (status) {
        goto cleanup;
    }

    // Run the setup hook if any
    if (options->setup_hook.bash) {
        VLOG_TRACE("kitchen", "executing setup hook\n");
        status = __setup_hook(kitchen, options);
        if (status) {
            goto cleanup;
        }
    }

    // write hash
    status = __write_hash(kitchen->host_hash_file, kitchen->hash);
    if (status) {
        goto cleanup;
    }

cleanup:
    kitchen_user_delete(&user);
    return status;
}

int kitchen_purge(struct kitchen_purge_options* options)
{
    struct kitchen_user user;
    struct list        recipes;
    struct list_item*  i;
    int                status;
    char*              kitchenPath;

    VLOG_DEBUG("kitchen", "kitchen_purge()\n");

    if (kitchen_user_new(&user)) {
        VLOG_ERROR("kitchen", "kitchen_purge: failed to get current user\n");
        return -1;
    }

    kitchenPath = strpathcombine(options->project_path, ".kitchen");
    if (kitchenPath == NULL) {
        VLOG_ERROR("kitchen", "kitchen_purge: failed to allocate memory for path\n");
        goto cleanup;
    }

    list_init(&recipes);
    status = platform_getfiles(kitchenPath, 0, &recipes);
    if (status) {
        // ignore this error, just means there is no cleanup to be done
        if (errno != ENOENT) {
            VLOG_ERROR("kitchen", "kitchen_purge: failed to get current recipes\n");
        }
        goto cleanup;
    }

    list_foreach (&recipes, i) {
        struct platform_file_entry* entry = (struct platform_file_entry*)i;
        if (!strcmp(entry->name, "output")) {
            continue;
        }
        __ensure_mounts_cleanup(kitchenPath, entry->name);
    }

    status = __clean_environment(kitchenPath);
    if (status) {
        VLOG_ERROR("kitchen", "purge: failed to clean path %s\n", kitchenPath);
        if (errno != ENOENT) {
            VLOG_ERROR("kitchen", "kitchen_purge: failed to remove the kitchen data\n");
            goto cleanup;
        }
    }

cleanup:
    kitchen_user_delete(&user);
    platform_getfiles_destroy(&recipes);
    free(kitchenPath);
    return 0;
}

int kitchen_recipe_clean(struct recipe* recipe, struct kitchen_clean_options* options)
{
    struct kitchen_user user;
    int                status;
    char*              kitchenPath;
    char               scratchPad[512];

    VLOG_DEBUG("kitchen", "kitchen_recipe_clean()\n");

    if (kitchen_user_new(&user)) {
        VLOG_ERROR("kitchen", "kitchen_recipe_clean: failed to get current user\n");
        return -1;
    }

    kitchenPath = strpathcombine(options->project_path, ".kitchen");
    if (kitchenPath == NULL) {
        VLOG_ERROR("kitchen", "kitchen_recipe_clean: failed to allocate memory for path\n");
        goto cleanup;
    }

    __ensure_mounts_cleanup(kitchenPath, options->name);
    status = __clean_environment(kitchenPath);
    if (status) {
        if (errno != ENOENT) {
            VLOG_ERROR("kitchen", "kitchen_recipe_clean: failed: %s\n", strerror(errno));
            goto cleanup;
        }
    }

cleanup:
    free(kitchenPath);
    kitchen_user_delete(&user);
    return 0;
}
