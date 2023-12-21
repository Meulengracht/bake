/**
 * Copyright 2023, Philip Meulengracht
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
#include "../private.h"
#include <chef/platform.h>
#include <fcntl.h>
#include <libingredient.h>
#include <liboven.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <vlog.h>

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


// <root>/.oven/output
// <root>/.oven/<package>/bin
// <root>/.oven/<package>/lib
// <root>/.oven/<package>/share
// <root>/.oven/<package>/usr/...
// <root>/.oven/<package>/target/
// <root>/.oven/<package>/target/ingredients
// <root>/.oven/<package>/chef/build
// <root>/.oven/<package>/chef/install => <root>/.oven/output
// <root>/.oven/<package>/chef/project => <root>
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
        VLOG_ERROR("oven", "__make_available: failed to open %s for writing: %s\n", pcPath, strerror(errno));
        free(pcPath);
        return -1;
    }

    cflags = __string_array_join(ingredient->options->inc_dirs, "-I{prefix}", " ");
    libs = __string_array_join(ingredient->options->lib_dirs, "-L{prefix}", " ");
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

static int __setup_ingredients(struct scratch* scratch, struct list* ingredients)
{
    struct list_item* i;
    int               status;

    if (ingredients == NULL) {
        return 0;
    }

    list_foreach(ingredients, i) {
        struct oven_ingredient* ovenIngredient = (struct oven_ingredient*)i;
        struct ingredient*      ingredient;
        const char*             targetPath = "";
        const char*             hostTargetPath = scratch->host_chroot;

        status = ingredient_open(ovenIngredient->file_path, &ingredient);
        if (status) {
            VLOG_ERROR("oven", "__setup_ingredients: failed to open %s\n", ovenIngredient->name);
            return -1;
        }

        // If the ingredient has a different platform or arch than host
        // then the ingredient should be installed differently
        if (strcmp(ingredient->package->platform, CHEF_PLATFORM_STR) ||
            strcmp(ingredient->package->arch, CHEF_ARCHITECTURE_STR)) {
            targetPath = scratch->target_ingredients_path;
            hostTargetPath = scratch->host_target_ingredients_path;
        }

        status = ingredient_unpack(ingredient, targetPath, NULL, NULL);
        if (status) {
            ingredient_close(ingredient);
            VLOG_ERROR("oven", "__setup_ingredients: failed to setup %s\n", ovenIngredient->name);
            return -1;
        }

        status = __make_available(hostTargetPath, targetPath, ingredient);
        ingredient_close(ingredient);
        if (status) {
            VLOG_ERROR("oven", "__setup_ingredients: failed to make %s available\n", ovenIngredient->name);
            return -1;
        }
    }
    return 0;
}

static char* __build_include_string(struct list* imports)
{
    struct list_item* i;
    char*             buffer;

    // --include=nano,gcc,clang,tcc,pcc,g++,git,make
    if (imports == NULL || imports->count == 0) {
        return NULL;
    }

    buffer = calloc(4096, 1); 
    if (buffer == NULL) {
        return NULL;
    }

    list_foreach(imports, i) {
        struct oven_package_import* import = (struct oven_package_import*)i;
        if (buffer[0] == 0) {
            strcpy(buffer, "--include=");
            strcat(buffer, import->name);
        } else {
            strcat(buffer, ",");
            strcat(buffer, import->name);
        }
    }
    return buffer;
}

static unsigned int __hash(unsigned int hash, const char* data, size_t length)
{
    for (unsigned int i = 0; i < length; i++) {
        unsigned char c = (unsigned char)data[i];
        hash = ((hash << 5) + hash) + c;
    }
    return hash;
}

// hash of ingredients and imports
static unsigned int __setup_hash(struct scratch_options* options)
{
    unsigned int      hash = 5381;
    struct list_item* i;

    // hash name
    hash = __hash(hash, options->name, strlen(options->name));

    // hash ingredients
    if (options->ingredients != NULL) {
        list_foreach(options->ingredients, i) {
            struct oven_ingredient* ovenIngredient = (struct oven_ingredient*)i;
            hash = __hash(hash, ovenIngredient->name, strlen(ovenIngredient->name));
        }
    }
    
    // hash imports
    if (options->imports != NULL) {
        list_foreach(options->imports, i) {
            struct oven_package_import* import = (struct oven_package_import*)i;
            hash = __hash(hash, import->name, strlen(import->name));
        }
    }
    return hash;
}

static unsigned int __read_hash(const char* name)
{
    char  scratchPad[512];
    FILE* hashFile;
    long  size;
    char* end = NULL;
    VLOG_TRACE("oven", "__read_hash()\n");

    snprintf(&scratchPad[0], sizeof(scratchPad), ".oven/%s/chef/.hash", name);
    hashFile = fopen(&scratchPad[0], "r");
    if (hashFile == NULL) {
        VLOG_TRACE("oven", "__read_hash: no hash file\n");
        return 0;
    }

    fseek(hashFile, 0, SEEK_END);
    size = ftell(hashFile);
    rewind(hashFile);

    if (size >= sizeof(scratchPad)) {
        VLOG_ERROR("oven", "__read_hash: the hash file was invalid\n");
        fclose(hashFile);
        return 0;
    }
    if (fread(&scratchPad[0], 1, size, hashFile) < size) {
        VLOG_ERROR("oven", "__read_hash: failed to read hash file\n");
        fclose(hashFile);
        return 0;
    }
    
    fclose(hashFile);
    return (unsigned int)strtoul(&scratchPad[0], &end, 10);
}

static int __write_hash(struct scratch_options* options)
{
    char         scratchPad[512];
    FILE*        hashFile;
    unsigned int hash;
    VLOG_TRACE("oven", "__write_hash(name=%s)\n", options->name);

    snprintf(&scratchPad[0], sizeof(scratchPad), ".oven/%s/chef/.hash", options->name);
    hashFile = fopen(&scratchPad[0], "w");
    if (hashFile == NULL) {
        VLOG_TRACE("oven", "__read_hash: no hash file");
        return 0;
    }

    hash = __setup_hash(options);
    fprintf(hashFile, "%u", hash);
    fclose(hashFile);
    return 0;
}

static int __should_skip_setup(struct scratch_options* options)
{
    unsigned int currentHash  = __setup_hash(options);
    unsigned int existingHash = __read_hash(options->name);
    return currentHash == existingHash;
}

static int __scratch_construct(struct scratch_options* options, struct scratch* scratch)
{
    char scratchPad[512];
    VLOG_DEBUG("oven", "__scratch_construct(name=%s)\n", options->name);

    snprintf(&scratchPad[0], sizeof(scratchPad), ".oven/%s", options->name);
    scratch->host_chroot = strdup(&scratchPad[0]);

    snprintf(&scratchPad[0], sizeof(scratchPad), ".oven/%s/target/ingredients", options->name);
    scratch->host_target_ingredients_path = strdup(&scratchPad[0]);

    snprintf(&scratchPad[0], sizeof(scratchPad), ".oven/%s/chef/build", options->name);
    scratch->host_build_path = strdup(&scratchPad[0]);

    snprintf(&scratchPad[0], sizeof(scratchPad), ".oven/%s/chef/install", options->name);
    scratch->host_install_path = strdup(&scratchPad[0]);

    snprintf(&scratchPad[0], sizeof(scratchPad), ".oven/%s/chef/.checkpoint", options->name);
    scratch->host_checkpoint_path = strdup(&scratchPad[0]);

    scratch->target_ingredients_path = strdup("/target/ingredients");
    scratch->project_root = strdup("/chef/project");
    scratch->build_root = strdup("/chef/build");
    scratch->install_root = strdup("/chef/install");
    scratch->os_base = options->os_base;
    return 0;
}

int scratch_setup(struct scratch_options* options, struct scratch* scratch)
{
    char  scratchPad[512];
    char* includes;
    int   status;
    VLOG_DEBUG("oven", "scratch_setup(name=%s)\n", options->name);

    if (__should_skip_setup(options)) {
        return __scratch_construct(options, scratch);
    }

    snprintf(&scratchPad[0], sizeof(scratchPad), ".oven/%s/target/ingredients", options->name);
    if (platform_mkdir(&scratchPad[0])) {
        VLOG_ERROR("oven", "scratch_setup: failed to create %s\n", &scratchPad[0]);
        return -1;
    }

    snprintf(&scratchPad[0], sizeof(scratchPad), ".oven/%s/chef/build", options->name);
    if (platform_mkdir(&scratchPad[0])) {
        VLOG_ERROR("oven", "scratch_setup: failed to create %s\n", &scratchPad[0]);
        return -1;
    }

    snprintf(&scratchPad[0], sizeof(scratchPad), ".oven/%s/chef/install", options->name);
    if (platform_symlink(&scratchPad[0], options->install_path, 1)) {
        VLOG_ERROR("oven", "scratch_setup: failed to link %s\n", &scratchPad[0]);
        return -1;
    }

    snprintf(&scratchPad[0], sizeof(scratchPad), ".oven/%s/chef/project", options->name);
    if (platform_symlink(&scratchPad[0], options->project_path, 1)) {
        VLOG_ERROR("oven", "scratch_setup: failed to link %s\n", &scratchPad[0]);
        return -1;
    }

    if (__scratch_construct(options, scratch)) {
        return -1;
    }

    // extract os/ingredients/toolchain
    if (__setup_ingredients(scratch, options->ingredients)) {
        return -1;
    }

    // write hash
    if (__write_hash(options)) {
        return -1;
    }
    return 0;
}

int scratch_enter(struct scratch* scratch)
{
    VLOG_DEBUG("oven", "scratch_enter(base=%i)\n", scratch->os_base);
    
    if (scratch->os_base) {
        // for an os-base we do not chroot, instead we allow full access
        // to the base operating system to allow the os-base to include all
        // it needs.
        return 0;
    }

    if (scratch->original_root_fd > 0) {
        VLOG_ERROR("oven", "scratch_enter: cannot recursively enter scratch root\n");
        return -1;
    }

    scratch->original_root_fd = open("/", __O_PATH);
    if (scratch->original_root_fd < 0) {
        VLOG_ERROR("oven", "scratch_enter: failed to get a handle on root: %s\n", strerror(errno));
        return -1;
    }

    if (chroot(scratch->host_chroot)) {
        VLOG_ERROR("oven", "scratch_enter: failed to change root environment to %s\n", scratch->host_chroot);
        return -1;
    }

    // Change working directory to the known project root
    if (chdir(scratch->project_root)) {
        VLOG_ERROR("oven", "scratch_enter: failed to change working directory to %s\n", scratch->project_root);
        return -1;
    }
    return 0;
}

int scratch_leave(struct scratch* scratch)
{
    VLOG_DEBUG("oven", "scratch_leave()\n");

    if (scratch->os_base) {
        // nothing to do for os-bases
        return 0;
    }
    
    if (scratch->original_root_fd <= 0) {
        return -1;
    }

    if (fchdir(scratch->original_root_fd)) {
        return -1;
    }
    if (chroot(".")) {
        return -1;
    }
    close(scratch->original_root_fd);
    scratch->original_root_fd = 0;
    return 0;
}

// debootstrap --variant=minbase --include=nano,gcc,clang,tcc,pcc,g++,git,make --arch=i386 stable /stable-chroot http://deb.debian.org/debian/
int scratch_setup_bootstrap(struct scratch_options* options, struct scratch* scratch)
{
    char  scratchPad[512];
    char* includes;
    int   status;
    VLOG_DEBUG("oven", "scratch_setup(name=%s)\n", options->name);

    if (platform_spawn("debootstrap", "--version", NULL, NULL)) {
        VLOG_ERROR("oven", "scratch_setup: \"debootstrap\" package must be installed\n");
        return -1;
    }

    if (__should_skip_setup(options)) {
        return __scratch_construct(options, scratch);
    }

    includes = __build_include_string(options->imports);
    if (includes != NULL) {
        snprintf(&scratchPad[0], sizeof(scratchPad), "--variant=minbase %s stable .oven/%s http://deb.debian.org/debian/", includes, options->name);
        free(includes);
    } else {
        snprintf(&scratchPad[0], sizeof(scratchPad), "--variant=minbase stable .oven/%s http://deb.debian.org/debian/", options->name);
    }

    status = platform_spawn("debootstrap", &scratchPad[0], NULL, NULL);
    if (status) {
        VLOG_ERROR("oven", "scratch_setup: \"debootstrap\" failed: %i\n", status);
        return -1;
    }

    snprintf(&scratchPad[0], sizeof(scratchPad), ".oven/%s/chef/build", options->name);
    if (platform_mkdir(&scratchPad[0])) {
        VLOG_ERROR("oven", "scratch_setup: failed to create %s\n", &scratchPad[0]);
        return -1;
    }

    snprintf(&scratchPad[0], sizeof(scratchPad), ".oven/%s/chef/install", options->name);
    if (platform_symlink(&scratchPad[0], options->install_path, 1)) {
        VLOG_ERROR("oven", "scratch_setup: failed to link %s\n", &scratchPad[0]);
        return -1;
    }

    snprintf(&scratchPad[0], sizeof(scratchPad), ".oven/%s/chef/project", options->name);
    if (platform_symlink(&scratchPad[0], options->project_path, 1)) {
        VLOG_ERROR("oven", "scratch_setup: failed to link %s\n", &scratchPad[0]);
        return -1;
    }

    if (__scratch_construct(options, scratch)) {
        return -1;
    }

    // extract os/ingredients/toolchain
    if (__setup_ingredients(scratch, options->ingredients)) {
        return -1;
    }

    // write hash
    if (__write_hash(options)) {
        return -1;
    }
    return 0;
}