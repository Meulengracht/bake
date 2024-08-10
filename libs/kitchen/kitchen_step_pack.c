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
#include <chef/kitchen.h>
#include <stdlib.h>
#include <string.h>
#include <vlog.h>
#include "pack/pack.h"

// include dirent.h for directory operations
#if defined(_WIN32) || defined(_WIN64)
#include <dirent_win32.h>
#else
#include <dirent.h>
#endif

static void __initialize_pack_options(
    struct kitchen*              kitchen,
    struct kitchen_pack_options* options,
    struct recipe*               recipe,
    struct recipe_pack*          pack)
{
    memset(options, 0, sizeof(struct kitchen_pack_options));
    options->name             = pack->name;
    options->sysroot_dir      = kitchen->host_chroot;
    options->output_dir       = kitchen->host_cwd;
    options->input_dir        = kitchen->host_install_path;
    options->ingredients_root = kitchen->host_build_ingredients_path;
    options->platform         = kitchen->target_platform;
    options->architecture     = kitchen->target_architecture;

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

static char* __source_pack_name(const char* root, const char* name)
{
    char tmp[4096] = { 0 };
    snprintf(&tmp[0], sizeof(tmp), "%s/%s.pack", root, name);
    return platform_strdup(&tmp[0]);
}

static char* __destination_pack_name(const char* root, const char* platform, const char* arch, const char* name)
{
    char tmp[4096] = { 0 };
    snprintf(&tmp[0], sizeof(tmp), "%s/%s_%s_%s.pack", root, name, platform, arch);
    return platform_strdup(&tmp[0]);
}

static int __move_pack(struct kitchen* kitchen, struct recipe_pack* pack)
{
    char* src = __source_pack_name(kitchen->host_install_root, pack->name);
    char* dst = __destination_pack_name(kitchen->host_cwd, kitchen->target_platform, kitchen->target_architecture, pack->name);
    int   status;

    if (src == NULL || dst == NULL) {
        status = -1;
        goto exit;
    }

    status = access(src, 0);
    if (status) {
        // If there was any previous errors then the package is not generated
        if (errno == ENOENT) {
            VLOG_DEBUG("kitchen", "__move_pack: no package was generated\n");
            status = 0;
            goto exit;
        }
        goto exit;
    }

    status = rename(src, dst);
    if (status) {
        VLOG_ERROR("kitchen", "__move_pack: failed to move package from %s => %s\n", src, dst);
    }

exit:
    free(src);
    free(dst);
    return status;
}

static int __matches_filters(const char* path, struct list* filters)
{
    struct list_item* item;

    if (filters->count == 0) {
        return 0; // YES! no filters means everything matches
    }

    list_foreach(filters, item) {
        struct list_item_string* filter = (struct list_item_string*)item;
        if (strfilter(filter->value, path, 0) != 0) {
            return -1;
        }
    }
    return 0;
}

int __copy_files_with_filters(const char* sourceRoot, const char* path, struct list* filters, const char* destinationRoot)
{
    // recursively iterate through the directory and copy all files
    // as long as they match the list of filters
    int            status = -1;
    struct dirent* entry;
    DIR*           dir;
    const char*    finalSource;
    const char*    finalDestination = NULL;
    
    finalSource = strpathcombine(sourceRoot, path);
    if (!finalSource) {
        goto cleanup;
    }

    finalDestination = strpathcombine(destinationRoot, path);
    if (!finalDestination) {
        goto cleanup;
    }

    dir = opendir(finalSource);
    if (!dir) {
        goto cleanup;
    }

    // make sure target is created
    if (platform_mkdir(finalDestination)) {
        goto cleanup;
    }

    while ((entry = readdir(dir)) != NULL) {
        const char* combinedSubPath;

        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        combinedSubPath = strpathcombine(path, entry->d_name);
        if (!combinedSubPath) {
            goto cleanup;
        }

        // does this match filters?
        if (__matches_filters(combinedSubPath, filters)) {
            free((void*)combinedSubPath);
            continue;
        }

        // oh ok, is it a directory?
        if (entry->d_type == DT_DIR) {
            status = __copy_files_with_filters(sourceRoot, combinedSubPath, filters, destinationRoot);
            free((void*)combinedSubPath);
            if (status) {
                goto cleanup;
            }
        } else {
            // ok, it's a file, copy it
            char* sourceFile      = strpathcombine(finalSource, entry->d_name);
            char* destinationFile = strpathcombine(finalDestination, entry->d_name);
            free((void*)combinedSubPath);
            if (!sourceFile || !destinationFile) {
                free((void*)sourceFile);
                goto cleanup;
            }

            status = platform_copyfile(sourceFile, destinationFile);
            free((void*)sourceFile);
            free((void*)destinationFile);
            if (status) {
                goto cleanup;
            }
        }
    }

    closedir(dir);
    status = 0;

cleanup:
    free((void*)finalSource);
    free((void*)finalDestination);
    return status;
}

int kitchen_recipe_pack(struct kitchen* kitchen, struct recipe* recipe)
{
    struct list_item* item;
    int               status;
    VLOG_DEBUG("kitchen", "kitchen_recipe_pack()\n");

    // include ingredients marked for packing
    list_foreach(&recipe->environment.runtime.ingredients, item) {
        struct recipe_ingredient* ingredient = (struct recipe_ingredient*)item;
        
        status = __copy_files_with_filters(
            kitchen->host_build_ingredients_path,
            NULL,
            &ingredient->filters,
            kitchen->host_install_path
        );
        if (status) {
            VLOG_ERROR("kitchen", "kitchen_recipe_pack: failed to include ingredient %s\n", ingredient->name);
            goto cleanup;
        }
    }

    list_foreach(&recipe->packs, item) {
        struct recipe_pack*         pack = (struct recipe_pack*)item;
        struct kitchen_pack_options packOptions;

        __initialize_pack_options(kitchen, &packOptions, recipe, pack);
        status = kitchen_pack(&packOptions);
        if (status) {
            VLOG_ERROR("kitchen", "kitchen_recipe_pack: failed to construct pack %s\n", pack->name);
            goto cleanup;
        }
    }

    // move packs out of the output directory and into root project folder
    list_foreach(&recipe->packs, item) {
        struct recipe_pack* pack = (struct recipe_pack*)item;
        if (__move_pack(kitchen, pack)) {
            VLOG_ERROR("kitchen", "kitchen_recipe_pack: failed to move pack %s to project directory\n", pack->name);
        }
    }

cleanup:
    return status;
}
