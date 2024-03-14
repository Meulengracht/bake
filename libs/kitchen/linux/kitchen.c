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

#define _GNU_SOURCE // needed for getresuid and friends

#include <chef/kitchen.h>
#include <chef/platform.h>
#include <libingredient.h>
#include <vlog.h>

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>

#include <pwd.h>
#include <sys/types.h>
#include <sys/mount.h>
#include <unistd.h>

struct __chef_user {
    char*        caller_name;
    unsigned int caller_uid;
    unsigned int caller_gid;

    char*        effective_name;
    unsigned int effective_uid;
    unsigned int effective_gid;
};

static int __chef_user_new(struct __chef_user* user)
{
    uid_t ruid, euid, suid;
    if (getresuid(&ruid, &euid, &suid)) {
        VLOG_ERROR("kitchen", "failed to retrieve user details: %s\n", strerror(errno));
        return -1;
    }
    VLOG_DEBUG("kitchen", "real: %u, effective: %u, saved: %u\n", ruid, euid, suid);

    struct passwd *caller = getpwuid(ruid);
    if (caller == NULL) {
        VLOG_ERROR("kitchen", "failed to retrieve current user details: %s\n", strerror(errno));
        return -1;
    }
    struct passwd *effective = getpwuid(euid);
    if (effective == NULL) {
        VLOG_ERROR("kitchen", "failed to retrieve executing user details: %s\n", strerror(errno));
        return -1;
    }

    // effective should be set to root
    if (effective->pw_uid != 0 && effective->pw_gid != 0) {
        VLOG_ERROR("kitchen", "bake must run under the root account/group\n");
        return -1;
    }

    // caller is the current actual user.
    user->caller_name = strdup(caller->pw_name);
    user->caller_uid = caller->pw_uid;
    user->caller_gid = caller->pw_gid;
    VLOG_DEBUG("kitchen", "caller: %u/%u (%s)\n", caller->pw_uid, caller->pw_gid, caller->pw_name);

    user->effective_name = strdup(effective->pw_name);
    user->effective_uid = effective->pw_uid;
    user->effective_gid = effective->pw_gid;
    VLOG_DEBUG("kitchen", "effective: %u/%u (%s)\n", effective->pw_uid, effective->pw_gid, effective->pw_name);
    return 0;
}

static int __chef_user_switch(unsigned int uid, unsigned int gid)
{
    int status;

    status = setgid(uid);
    if (status) {
        VLOG_ERROR("kitchen", "failed setgid: %s\n", strerror(errno));
        return status;
    }
    status = setuid(gid);
    if (status) {
        VLOG_ERROR("kitchen", "failed setuid: %s\n", strerror(errno));
        return status;
    }
    return 0;
}

static int __chef_user_switch_root(struct __chef_user* user)
{
    return __chef_user_switch(user->effective_uid, user->effective_gid);
}

static int __chef_user_restore(struct __chef_user* user)
{
    return __chef_user_switch(user->caller_uid, user->caller_uid);
}

static void __chef_user_delete(struct __chef_user* user)
{
    free(user->caller_name);
    free(user->effective_name);
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

static char* __string_array_join(const char* const* items, const char* prefix, const char* separator)
{
    char* buffer;

    buffer = calloc(4096, 1); 
    if (buffer == NULL) {
        return NULL;
    }

    for (int i = 0; items[i]; i++) {
        if (buffer[0] == 0) {
            strcpy(buffer, prefix);
        } else {
            strcat(buffer, separator);
            strcat(buffer, prefix);
        }
        strcat(buffer, items[i]);
    }
    return buffer;
}

static int __make_available(const char* hostRoot, const char* root, struct ingredient* ingredient)
{
    FILE* file;
    char  pcName[256];
    char* pcPath;
    int   written;
    int   status;
    char* cflags;
    char* libs;

    if (ingredient->options == NULL) {
        // Can't add a pkg-config file if the ingredient didn't specify any
        // options for consumers.
        // TODO: Add defaults?
        return 0;
    }

    // The package name specified on the pkg-config command line is defined 
    // to be the name of the metadata file, minus the .pc extension. Optionally
    // the version can be appended as name-1.0
    written = snprintf(&pcName[0], sizeof(pcName) - 1, "%s.pc", ingredient->package->package);
    if (written == (sizeof(pcName) - 1)) {
        errno = E2BIG;
        return -1;
    }
    
    pcPath = strpathjoin(hostRoot, "/usr/share/pkgconfig/", &pcName[0], NULL);
    if (pcPath == NULL) {
        return -1;
    }
    
    file = fopen(pcPath, "w");
    if(!file) {
        VLOG_ERROR("kitchen", "__make_available: failed to open %s for writing: %s\n", pcPath, strerror(errno));
        free(pcPath);
        return -1;
    }

    cflags = __string_array_join((const char* const*)ingredient->options->inc_dirs, "-I{prefix}", " ");
    libs = __string_array_join((const char* const*)ingredient->options->lib_dirs, "-L{prefix}", " ");
    if (cflags == NULL || libs == NULL) {
        free(cflags);
        free(libs);
        fclose(file);
        return -1;
    }

    fprintf(file, "# generated by chef, please do not manually modify this\n");
    fprintf(file, "prefix=%s\n", root);

    fprintf(file, "Name: %s\n", ingredient->package->package);
    fprintf(file, "Description: %s by %s\n", ingredient->package->package, ingredient->package->publisher);
    fprintf(file, "Version: %i.%i.%i\n", ingredient->version->major, ingredient->version->minor, ingredient->version->patch);
    fprintf(file, "Cflags: %s\n", cflags);
    fprintf(file, "Libs: %s\n", libs);
    free(cflags);
    free(libs);
    return fclose(file);
}

static int __setup_ingredient(struct list* ingredients, const char* hostPath, const char* chrootPath)
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
            VLOG_TRACE("kitchen", "__setup_ingredients: skipping %s of type %i", kitchenIngredient->name, ingredient->package->type);
            ingredient_close(ingredient);
            continue;
        }

        status = ingredient_unpack(ingredient, hostPath, NULL, NULL);
        if (status) {
            ingredient_close(ingredient);
            VLOG_ERROR("kitchen", "__setup_ingredients: failed to setup %s\n", kitchenIngredient->name);
            return -1;
        }

        status = __make_available(hostPath, chrootPath, ingredient);
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

    status = __setup_ingredient(&options->host_ingredients, kitchen->host_chroot, ".");
    if (status) {
        return status;
    }

    status = __setup_ingredient(&options->build_ingredients, kitchen->host_build_ingredients_path, kitchen->build_ingredients_path);
    if (status) {
        return status;
    }

    status = __setup_toolchains(&options->build_ingredients, kitchen->host_build_toolchains_path);
    if (status) {
        return status;
    }

    status = __setup_ingredient(&options->runtime_ingredients, kitchen->host_install_path, kitchen->install_root);
    if (status) {
        return status;
    }
    return 0;
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

static unsigned int __read_hash(const char* name)
{
    char  buff[512];
    FILE* hashFile;
    long  size;
    char* end = NULL;
    VLOG_DEBUG("kitchen", "__read_hash()\n");

    snprintf(&buff[0], sizeof(buff), ".kitchen/%s/chef/.hash", name);
    hashFile = fopen(&buff[0], "r");
    if (hashFile == NULL) {
        VLOG_DEBUG("kitchen", "__read_hash: no hash file\n");
        return 0;
    }

    fseek(hashFile, 0, SEEK_END);
    size = ftell(hashFile);
    rewind(hashFile);

    if (size >= sizeof(buff)) {
        VLOG_ERROR("kitchen", "__read_hash: the hash file was invalid\n");
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

static int __write_hash(struct kitchen_setup_options* options)
{
    char         kitchenPad[512];
    FILE*        hashFile;
    unsigned int hash;
    VLOG_DEBUG("kitchen", "__write_hash(name=%s)\n", options->name);

    snprintf(&kitchenPad[0], sizeof(kitchenPad), ".kitchen/%s/chef/.hash", options->name);
    hashFile = fopen(&kitchenPad[0], "w");
    if (hashFile == NULL) {
        VLOG_DEBUG("kitchen", "__write_hash: no hash file");
        return 0;
    }

    hash = __setup_hash(options);
    fprintf(hashFile, "%u", hash);
    fclose(hashFile);
    return 0;
}

static int __should_skip_setup(struct kitchen_setup_options* options)
{
    unsigned int currentHash  = __setup_hash(options);
    unsigned int existingHash = __read_hash(options->name);
    return currentHash == existingHash;
}

// <root>/.kitchen/output
// <root>/.kitchen/<recipe>/bin
// <root>/.kitchen/<recipe>/lib
// <root>/.kitchen/<recipe>/share
// <root>/.kitchen/<recipe>/usr/...
// <root>/.kitchen/<recipe>/chef/build
// <root>/.kitchen/<recipe>/chef/build/ingredients
// <root>/.kitchen/<recipe>/chef/build/toolchains
// <root>/.kitchen/<recipe>/chef/install => <root>/.kitchen/output
// <root>/.kitchen/<recipe>/chef/project => <root>
static int __kitchen_construct(struct kitchen_setup_options* options, struct kitchen* kitchen)
{
    char buff[2048];
    VLOG_DEBUG("kitchen", "__kitchen_construct(name=%s)\n", options->name);

    snprintf(&buff[0], sizeof(buff), "%s/.kitchen/%s", options->project_path, options->name);
    kitchen->host_chroot = strdup(&buff[0]);

    snprintf(&buff[0], sizeof(buff), "%s/.kitchen/%s/chef/build", options->project_path, options->name);
    kitchen->host_build_path = strdup(&buff[0]);

    snprintf(&buff[0], sizeof(buff), "%s/.kitchen/%s/chef/build/ingredients", options->project_path, options->name);
    kitchen->host_build_ingredients_path = strdup(&buff[0]);

    snprintf(&buff[0], sizeof(buff), "%s/.kitchen/%s/chef/build/toolchains", options->project_path, options->name);
    kitchen->host_build_toolchains_path = strdup(&buff[0]);

    snprintf(&buff[0], sizeof(buff), "%s/.kitchen/%s/chef/project", options->project_path, options->name);
    kitchen->host_project_path = strdup(&buff[0]);

    snprintf(&buff[0], sizeof(buff), "%s/.kitchen/%s/chef/install", options->project_path, options->name);
    kitchen->host_install_path = strdup(&buff[0]);

    snprintf(&buff[0], sizeof(buff), "%s/.kitchen/%s/chef", options->project_path, options->name);
    kitchen->host_checkpoint_path = strdup(&buff[0]);

    kitchen->project_root = strdup("/chef/project");
    kitchen->build_root = strdup("/chef/build");
    kitchen->build_ingredients_path = strdup("/chef/build/ingredients");
    kitchen->build_toolchains_path = strdup("/chef/build/toolchains");
    kitchen->install_root = strdup("/chef/install");
    kitchen->checkpoint_root = strdup("/chef");
    kitchen->confined = options->confined;
    return 0;
}

static char* __build_include_string(struct list* packages)
{
    struct list_item* i;
    char*             buffer;

    // --include=nano,gcc,clang,tcc,pcc,g++,git,make
    if (packages == NULL || packages->count == 0) {
        return NULL;
    }

    buffer = calloc(4096, 1); 
    if (buffer == NULL) {
        return NULL;
    }

    list_foreach(packages, i) {
        struct oven_value_item* pkg = (struct oven_value_item*)i;
        if (buffer[0] == 0) {
            strcpy(buffer, "--include=");
            strcat(buffer, pkg->value);
        } else {
            strcat(buffer, ",");
            strcat(buffer, pkg->value);
        }
    }
    return buffer;
}

static int __clean_environment(const char* chrootPath)
{
    int status;
    
    // remove the root of the chroot
    // ignore if the directory doesn't exist
    status = platform_rmdir(chrootPath);
    if (status && errno != ENOENT) {
        return status;
    }

    return 0;
}

static int __setup_environment(struct list* packages, int confined, const char* chrootPath)
{
    char  scratchPad[512];
    char* includes;
    int   status;

    // If we are running unconfined we don't setup environment
    if (!confined) {
        return 0;
    }

    status = platform_spawn("debootstrap", "--version", NULL, NULL);
    if (status) {
        VLOG_ERROR("kitchen", "__setup_environment: \"debootstrap\" package must be installed\n");
        return status;
    }

    includes = __build_include_string(packages);
    if (includes != NULL) {
        snprintf(&scratchPad[0], sizeof(scratchPad), "--variant=minbase %s stable %s http://deb.debian.org/debian/", includes, chrootPath);
        free(includes);
    } else {
        snprintf(&scratchPad[0], sizeof(scratchPad), "--variant=minbase stable %s http://deb.debian.org/debian/", chrootPath);
    }

    status = platform_spawn("debootstrap", &scratchPad[0], NULL, NULL);
    if (status) {
        VLOG_ERROR("kitchen", "__setup_environment: \"debootstrap\" failed: %i\n", status);
    }
    return status;
}

static int __ensure_hostdirs(struct kitchen* kitchen)
{
    if (platform_mkdir(kitchen->host_build_ingredients_path)) {
        VLOG_ERROR("kitchen", "kitchen_setup: failed to create %s\n", kitchen->host_build_ingredients_path);
        return -1;
    }

    if (platform_mkdir(kitchen->host_build_toolchains_path)) {
        VLOG_ERROR("kitchen", "kitchen_setup: failed to create %s\n", kitchen->host_build_toolchains_path);
        return -1;
    }

    if (platform_mkdir(kitchen->host_install_path)) {
        VLOG_ERROR("kitchen", "kitchen_setup: failed to create %s\n", kitchen->host_install_path);
        return -1;
    }


    if (platform_mkdir(kitchen->host_project_path)) {
        VLOG_ERROR("kitchen", "kitchen_setup: failed to create %s\n", kitchen->host_project_path);
        return -1;
    }

    if (platform_mkdir(".kitchen/output")) {
        VLOG_ERROR("kitchen", "kitchen_setup: failed to create .kitchen/output\n");
        return -1;
    }
    return 0;
}

static int __ensure_mounted_dirs(struct kitchen* kitchen, const char* projectPath)
{
    if (mount(".kitchen/output", kitchen->host_install_path, NULL, MS_BIND | MS_SHARED, NULL)) {
        VLOG_ERROR("kitchen", "kitchen_setup: failed to mount %s\n", kitchen->host_install_path);
        return -1;
    }

    if (mount(projectPath, kitchen->host_project_path, NULL, MS_BIND | MS_PRIVATE | MS_RDONLY, NULL)) {
        VLOG_ERROR("kitchen", "kitchen_setup: failed to mount %s\n", kitchen->host_project_path);
        return -1;
    }
    return 0;
}

static void __ensure_mounts_cleanup(struct kitchen* kitchen)
{
    if (umount(kitchen->host_install_path)) {
        VLOG_DEBUG("kitchen", "kitchen_setup: failed to unmount %s\n", kitchen->host_install_path);
    }
    if (umount(kitchen->host_project_path)) {
        VLOG_DEBUG("kitchen", "kitchen_setup: failed to unmount %s\n", kitchen->host_project_path);
    }
}

int kitchen_setup(struct kitchen_setup_options* options, struct kitchen* kitchen)
{
    struct __chef_user user;
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
            .install_root = options->confined ? kitchen->install_root : kitchen->host_install_path,
            .checkpoint_root = options->confined ? kitchen->checkpoint_root : kitchen->host_checkpoint_path,
        }
    });
    if (status) {
        VLOG_ERROR("kitchen", "failed to initialize oven: %s\n", strerror(errno));
        return -1;
    }
    atexit(oven_cleanup);

    if (__should_skip_setup(options)) {
        return 0;
    }

    if (__chef_user_new(&user)) {
        VLOG_ERROR("kitchen", "kitchen_setup: failed to get current user\n");
        return -1;
    }

    status = __chef_user_switch_root(&user);
    if (status) {
        VLOG_ERROR("kitchen", "__setup_environment: failed to switch to root\n");
        __chef_user_delete(&user);
        return -1;
    }

    VLOG_TRACE("kitchen", "cleaning project environment\n");
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

    status = __ensure_hostdirs(kitchen);
    if (status) {
        VLOG_ERROR("kitchen", "kitchen_setup: failed to create host directories\n");
        goto cleanup;
    }

    status = __ensure_mounted_dirs(kitchen, options->project_path);
    if (status) {
        VLOG_ERROR("kitchen", "kitchen_setup: failed to create host symlinks\n");
        goto cleanup;
    }

    // extract os/ingredients/toolchain
    VLOG_TRACE("kitchen", "installing project ingredients\n");
    status = __setup_ingredients(kitchen, options);
    if (status) {
        goto cleanup;
    }

    // write hash
    status = __write_hash(options);
    if (status) {
        goto cleanup;
    }

cleanup:
    __chef_user_restore(&user);
    __chef_user_delete(&user);
    return 0;
}

int kitchen_purge(struct kitchen_purge_options* options)
{
    struct __chef_user user;
    int                status;
    char*              kitchenPath;
    char               scratchPad[512];

    VLOG_DEBUG("kitchen", "kitchen_purge()\n");

    if (__chef_user_new(&user)) {
        VLOG_ERROR("kitchen", "kitchen_purge: failed to get current user\n");
        return -1;
    }

    status = __chef_user_switch_root(&user);
    if (status) {
        VLOG_ERROR("kitchen", "kitchen_purge: failed to switch to root\n");
        __chef_user_delete(&user);
        return -1;
    }

    kitchenPath = strpathcombine(options->project_path, ".kitchen");
    if (kitchenPath == NULL) {
        VLOG_ERROR("kitchen", "kitchen_purge: failed to allocate memory for path\n");
        goto cleanup;
    }

    snprintf(&scratchPad[0], sizeof(scratchPad), "-A --recursive %s", kitchenPath);
    status = platform_spawn("umount", &scratchPad[0], NULL, NULL);
    if (status) {
        VLOG_ERROR("kitchen", "kitchen_purge: failed to unmount all kitchen mounts\n");
        return status;
    }

    status = platform_rmdir(kitchenPath);
    if (status) {
        if (errno != ENOENT) {
            VLOG_ERROR("kitchen", "kitchen_purge: failed: %s\n", strerror(errno));
            goto cleanup;
        }
    }

cleanup:
    free(kitchenPath);
    __chef_user_restore(&user);
    __chef_user_delete(&user);
    return 0;
}

static int __start_cooking(struct kitchen* kitchen)
{
    VLOG_DEBUG("kitchen", "__start_cooking(confined=%i)\n", kitchen->confined);
    
    if (!kitchen->confined) {
        // for an unconfined we do not chroot, instead we allow full access
        // to the base operating system to allow the the part to include all
        // it needs.
        return 0;
    }

    if (kitchen->original_root_fd > 0) {
        VLOG_ERROR("kitchen", "kitchen_enter: cannot recursively enter kitchen root\n");
        return -1;
    }

    kitchen->original_root_fd = open("/", __O_PATH);
    if (kitchen->original_root_fd < 0) {
        VLOG_ERROR("kitchen", "kitchen_enter: failed to get a handle on root: %s\n", strerror(errno));
        return -1;
    }

    if (chroot(kitchen->host_chroot)) {
        VLOG_ERROR("kitchen", "kitchen_enter: failed to change root environment to %s\n", kitchen->host_chroot);
        return -1;
    }

    // Change working directory to the known project root
    if (chdir(kitchen->project_root)) {
        VLOG_ERROR("kitchen", "kitchen_enter: failed to change working directory to %s\n", kitchen->project_root);
        return -1;
    }
    return 0;
}

static int __end_cooking(struct kitchen* kitchen)
{
    VLOG_DEBUG("kitchen", "__end_cooking()\n");

    if (!kitchen->confined) {
        // nothing to do for unconfined
        return 0;
    }
    
    if (kitchen->original_root_fd <= 0) {
        return -1;
    }

    if (fchdir(kitchen->original_root_fd)) {
        return -1;
    }
    if (chroot(".")) {
        return -1;
    }
    close(kitchen->original_root_fd);
    kitchen->original_root_fd = 0;
    return 0;
}

static void __initialize_recipe_options(struct oven_recipe_options* options, struct recipe_part* part)
{
    options->name          = part->name;
    options->relative_path = part->path;
    options->toolchain     = part->toolchain;
}

static void __destroy_recipe_options(struct oven_recipe_options* options)
{
    free((void*)options->toolchain);
}

static int __reset_steps(struct list* steps, enum recipe_step_type stepType, const char* name);

static int __step_depends_on(struct list* dependencies, const char* step)
{
    struct list_item* item;
    VLOG_DEBUG("kitchen", "__step_depends_on(step=%s)\n", step);

    list_foreach(dependencies, item) {
        struct oven_value_item* value = (struct oven_value_item*)item;
        if (strcmp(value->value, step) == 0) {
            // OK this step depends on the step we are resetting
            // so reset this step too
            return 1;
        }
    }
    return 0;
}

static int __reset_depending_steps(struct list* steps, const char* name)
{
    struct list_item* item;
    int               status;
    VLOG_DEBUG("kitchen", "__reset_depending_steps(name=%s)\n", name);

    list_foreach(steps, item) {
        struct recipe_step* recipeStep = (struct recipe_step*)item;

        // skip ourselves
        if (strcmp(recipeStep->name, name) != 0) {
            if (__step_depends_on(&recipeStep->depends, name)) {
                status = __reset_steps(steps, RECIPE_STEP_TYPE_UNKNOWN, recipeStep->name);
                if (status) {
                    VLOG_ERROR("bake", "failed to reset step %s\n", recipeStep->name);
                    return status;
                }
            }
        }
    }
    return 0;
}

static int __reset_steps(struct list* steps, enum recipe_step_type stepType, const char* name)
{
    struct list_item* item;
    int               status;
    VLOG_DEBUG("kitchen", "__reset_steps(name=%s)\n", name);

    list_foreach(steps, item) {
        struct recipe_step* recipeStep = (struct recipe_step*)item;
        if ((stepType == RECIPE_STEP_TYPE_UNKNOWN) || (recipeStep->type == stepType) ||
            (name && strcmp(recipeStep->name, name) == 0)) {
            // this should be deleted
            status = oven_clear_recipe_checkpoint(recipeStep->name);
            if (status) {
                VLOG_ERROR("bake", "failed to clear checkpoint %s\n", recipeStep->name);
                return status;
            }

            // clear dependencies
            status = __reset_depending_steps(steps, recipeStep->name);
        }
    }
    return 0;
}

int kitchen_recipe_prepare(struct kitchen* kitchen, struct recipe* recipe, enum recipe_step_type stepType)
{
    struct oven_recipe_options options;
    struct list_item*          item;
    int                        status;
    VLOG_DEBUG("kitchen", "kitchen_recipe_prepare()\n");

    if (stepType == RECIPE_STEP_TYPE_UNKNOWN) {
        return 0;
    }

    status = __start_cooking(kitchen);
    if (status) {
        VLOG_ERROR("kitchen", "__start_cooking failed with code: %i", status);
        return status;
    }

    list_foreach(&recipe->parts, item) {
        struct recipe_part* part = (struct recipe_part*)item;

        __initialize_recipe_options(&options, part);
        status = oven_recipe_start(&options);
        __destroy_recipe_options(&options);

        if (status) {
            break;
        }

        status = __reset_steps(&part->steps, stepType, NULL);
        oven_recipe_end();

        if (status) {
            VLOG_ERROR("bake", "failed to build recipe %s\n", part->name);
            break;
        }
    }

    if (__end_cooking(kitchen)) {
        VLOG_ERROR("kitchen", "__end_cooking failed");
    }
    return status;
}

static void __initialize_generator_options(struct oven_generate_options* options, struct recipe_step* step)
{
    options->name           = step->name;
    options->profile        = NULL;
    options->system         = step->system;
    options->system_options = &step->options;
    options->arguments      = &step->arguments;
    options->environment    = &step->env_keypairs;
}

static void __initialize_build_options(struct oven_build_options* options, struct recipe_step* step)
{
    options->name           = step->name;
    options->profile        = NULL;
    options->system         = step->system;
    options->system_options = &step->options;
    options->arguments      = &step->arguments;
    options->environment    = &step->env_keypairs;
}

static void __initialize_script_options(struct oven_script_options* options, struct recipe_step* step)
{
    options->name   = step->name;
    options->script = step->script;
}

static int __make_recipe_steps(struct list* steps)
{
    struct list_item* item;
    int               status;
    VLOG_DEBUG("kitchen", "__make_recipe_steps()\n");
    
    list_foreach(steps, item) {
        struct recipe_step* step = (struct recipe_step*)item;
        VLOG_TRACE("bake", "executing step '%s'\n", step->system);

        if (step->type == RECIPE_STEP_TYPE_GENERATE) {
            struct oven_generate_options genOptions;
            __initialize_generator_options(&genOptions, step);
            status = oven_configure(&genOptions);
            if (status) {
                VLOG_ERROR("bake", "failed to configure target: %s\n", step->system);
                return status;
            }
        } else if (step->type == RECIPE_STEP_TYPE_BUILD) {
            struct oven_build_options buildOptions;
            __initialize_build_options(&buildOptions, step);
            status = oven_build(&buildOptions);
            if (status) {
                VLOG_ERROR("bake", "failed to build target: %s\n", step->system);
                return status;
            }
        } else if (step->type == RECIPE_STEP_TYPE_SCRIPT) {
            struct oven_script_options scriptOptions;
            __initialize_script_options(&scriptOptions, step);
            status = oven_script(&scriptOptions);
            if (status) {
                VLOG_ERROR("bake", "failed to execute script\n");
                return status;
            }
        }
    }
    
    return 0;
}

int kitchen_recipe_make(struct kitchen* kitchen, struct recipe* recipe)
{
    struct oven_recipe_options options;
    struct list_item*          item;
    int                        status;
    VLOG_DEBUG("kitchen", "kitchen_recipe_make()\n");

    status = __start_cooking(kitchen);
    if (status) {
        VLOG_ERROR("kitchen", "__start_cooking failed with code: %i", status);
        return status;
    }

    list_foreach(&recipe->parts, item) {
        struct recipe_part* part = (struct recipe_part*)item;

        __initialize_recipe_options(&options, part);
        status = oven_recipe_start(&options);
        __destroy_recipe_options(&options);

        if (status) {
            break;
        }

        status = __make_recipe_steps(&part->steps);
        oven_recipe_end();

        if (status) {
            VLOG_ERROR("bake", "failed to build recipe %s\n", part->name);
            break;
        }
    }

    if (__end_cooking(kitchen)) {
        VLOG_ERROR("kitchen", "__end_cooking failed");
    }
    return status;
}

static void __initialize_pack_options(
    struct oven_pack_options* options, 
    struct recipe*            recipe,
    struct recipe_pack*       pack)
{
    memset(options, 0, sizeof(struct oven_pack_options));
    options->name             = pack->name;
    options->type             = pack->type;
    options->summary          = recipe->project.summary;
    options->description      = recipe->project.description;
    options->icon             = recipe->project.icon;
    options->version          = recipe->project.version;
    options->license          = recipe->project.license;
    options->eula             = recipe->project.eula;
    options->maintainer       = recipe->project.author;
    options->maintainer_email = recipe->project.email;
    options->homepage         = recipe->project.url;
    options->filters          = &pack->filters;
    options->commands         = &pack->commands;
    
    if (pack->type == CHEF_PACKAGE_TYPE_INGREDIENT) {
        options->bin_dirs = &pack->options.bin_dirs;
        options->inc_dirs = &pack->options.inc_dirs;
        options->lib_dirs = &pack->options.lib_dirs;
        options->compiler_flags = &pack->options.compiler_flags;
        options->linker_flags = &pack->options.linker_flags;
    }
}

int kitchen_recipe_pack(struct kitchen* kitchen, struct recipe* recipe)
{
    struct oven_pack_options packOptions;
    struct list_item*        item;
    int                      status;
    VLOG_DEBUG("kitchen", "kitchen_recipe_pack()\n");

    status = __start_cooking(kitchen);
    if (status) {
        VLOG_ERROR("kitchen", "__start_cooking failed with code: %i", status);
        return status;
    }

    // include ingredients marked for packing
    list_foreach(&recipe->environment.runtime.ingredients, item) {
        struct recipe_ingredient* ingredient = (struct recipe_ingredient*)item;
        
        status = oven_include_filters(&ingredient->filters);
        if (status) {
            VLOG_ERROR("bake", "failed to include ingredient %s\n", ingredient->name);
            break;
        }
    }

    if (status) {
        goto error;
    }

    list_foreach(&recipe->packs, item) {
        struct recipe_pack* pack = (struct recipe_pack*)item;

        __initialize_pack_options(&packOptions, recipe, pack);
        status = oven_pack(&packOptions);
        if (status) {
            VLOG_ERROR("bake", "failed to construct pack %s\n", pack->name);
            break;
        }
    }

error:
    if (__end_cooking(kitchen)) {
        VLOG_ERROR("kitchen", "__end_cooking failed");
    }
    return status;
}

int kitchen_recipe_clean(struct recipe* recipe, struct kitchen_clean_options* options)
{
    return 0;
}
