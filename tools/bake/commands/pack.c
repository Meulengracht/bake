/**
 * Copyright, Philip Meulengracht
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
#include <chef/cvd.h>
#include <chef/dirs.h>
#include <chef/pack.h>
#include <chef/platform.h>
#include <chef/recipe.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <vlog.h>

#include "commands.h"

// include dirent.h for directory operations
#if defined(CHEF_ON_WINDOWS)
#include <dirent_win32.h>
#else
#include <dirent.h>
#endif

static void __print_help(void)
{
    printf("Usage: bake pack [options] <dir>\n");
    printf("\n");
    printf("Options:\n");
    printf("  --purge\n");
    printf("      cleans all active recipes in the kitchen area\n");
    printf("  -cc, --cross-compile\n");
    printf("      Cross-compile for another platform or/and architecture. This switch\n");
    printf("      can be used with two different formats, either just like\n");
    printf("      --cross-compile=arch or --cross-compile=platform/arch\n");
    printf("  -h, --help\n");
    printf("      Shows this help message\n");
}

static void __cleanup_systems(int sig)
{
    // printing as a part of a signal handler is not safe
    // but we live dangerously
    printf("termination requested, cleaning up\n");

    // Do a quick exit, which is recommended to do in signal handlers
    // and use the signal as the exit code
    _Exit(-sig);
}

static void __initialize_pack_options(
    struct __bake_build_context* bctx,
    struct __pack_options*       options,
    struct recipe_pack*          pack)
{
    memset(options, 0, sizeof(struct __pack_options));
    options->name             = pack->name;
    options->sysroot_dir      = bctx->rootfs_path;
    options->output_dir       = bctx->host_cwd;
    options->input_dir        = bctx->install_path;
    options->ingredients_root = bctx->build_ingredients_path;
    options->platform         = bctx->target_platform;
    options->architecture     = bctx->target_architecture;

    options->type             = pack->type;
    options->summary          = bctx->recipe->project.summary;
    options->description      = bctx->recipe->project.description;
    options->icon             = bctx->recipe->project.icon;
    options->version          = bctx->recipe->project.version;
    options->license          = bctx->recipe->project.license;
    options->eula             = bctx->recipe->project.eula;
    options->maintainer       = bctx->recipe->project.author;
    options->maintainer_email = bctx->recipe->project.email;
    options->homepage         = bctx->recipe->project.url;
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

static int __copy_files_with_filters(const char* sourceRoot, const char* path, struct list* filters, const char* destinationRoot)
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

int pack_main(int argc, char** argv, char** envp, struct bake_command_options* options)
{
    struct __bake_build_context* bctx;
    struct list_item*            item;
    struct build_cache*          cache = NULL;
    char*                        path = NULL;
    const char*                  arch;
    int                          status;

    // handle individual help command
    if (argc > 2) {
        for (int i = 2; i < argc; i++) {
            if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
                __print_help();
                return 0;
            } else if (argv[i][0] != '-') {
                path = argv[i];
            }
        }
    }

    if (path == NULL) {
        fprintf(stderr, "bake: no path provided\n");
        __print_help();
        return -1;
    }

    if (options->recipe == NULL) {
        fprintf(stderr, "bake: no recipe provided\n");
        __print_help();
        return -1;
    }

    // get the architecture from the list
    arch = ((struct list_item_string*)options->architectures.head)->value;

    // we need to load the recipe cache
    status = build_cache_create(options->recipe, options->cwd, &cache);
    if (status) {
        VLOG_ERROR("kitchen", "failed to initialize build cache\n");
        return -1;
    }

    // then construct the options
    VLOG_DEBUG("bake", "platform=%s, architecture=%s\n", options->platform, arch);
    bctx = build_context_create(&(struct __bake_build_options) {
        .cwd = options->cwd,
        .envp = (const char* const*)envp,
        .recipe = options->recipe,
        .recipe_path = options->recipe_path,
        .build_cache = cache,
        .target_platform = options->platform,
        .target_architecture = arch,
        .cvd_address = NULL
    });
    if (bctx == NULL) {
        VLOG_ERROR("bake", "failed to initialize build context: %s\n", strerror(errno));
        return -1;
    }

    // include ingredients marked for packing
    list_foreach(&options->recipe->environment.runtime.ingredients, item) {
        struct recipe_ingredient* ingredient = (struct recipe_ingredient*)item;
        
        status = __copy_files_with_filters(
            bctx->build_ingredients_path,
            NULL,
            &ingredient->filters,
            bctx->install_path
        );
        if (status) {
            VLOG_ERROR("bake", "kitchen_recipe_pack: failed to include ingredient %s\n", ingredient->name);
            goto cleanup;
        }
    }

    list_foreach(&options->recipe->packs, item) {
        struct recipe_pack*   pack = (struct recipe_pack*)item;
        struct __pack_options packOptions;

        __initialize_pack_options(bctx, &packOptions, pack);
        status = bake_pack(&packOptions);
        if (status) {
            VLOG_ERROR("bake", "kitchen_recipe_pack: failed to construct pack %s\n", pack->name);
            goto cleanup;
        }
    }

cleanup:
    return status;
}
