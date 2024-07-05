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

#include <chef/list.h>
#include <chef/kitchen.h>
#include <stdlib.h>
#include <string.h>
#include "steps.h"
#include <vlog.h>

static void __initialize_pack_options(
    struct oven_pack_options* options, 
    struct recipe*            recipe,
    struct recipe_pack*       pack,
    const char*               outputPath)
{
    memset(options, 0, sizeof(struct oven_pack_options));
    options->name             = pack->name;
    options->pack_dir         = outputPath;
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
    return strdup(&tmp[0]);
}

static char* __destination_pack_name(const char* root, const char* platform, const char* arch, const char* name)
{
    char tmp[4096] = { 0 };
    snprintf(&tmp[0], sizeof(tmp), "%s/%s_%s_%s.pack", root, name, platform, arch);
    return strdup(&tmp[0]);
}

static int __move_pack(struct kitchen* kitchen, struct recipe_pack* pack)
{
    char* src = __source_pack_name(kitchen->shared_output_path, pack->name);
    char* dst = __destination_pack_name(kitchen->real_project_path, kitchen->target_platform, kitchen->target_architecture, pack->name);
    int   status;

    if (src == NULL || dst == NULL) {
        status = -1;
        goto exit;
    }

    status = rename(src, dst);
    if (status) {
        VLOG_DEBUG("kitchen", "__move_pack: %s => %s\n", src, dst);
    }

exit:
    free(src);
    free(dst);
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
        
        status = oven_include_filters(&ingredient->filters);
        if (status) {
            VLOG_ERROR("bake", "kitchen_recipe_pack: failed to include ingredient %s\n", ingredient->name);
            kitchen_cooking_end(kitchen);
            goto cleanup;
        }
    }

    list_foreach(&recipe->packs, item) {
        struct recipe_pack*      pack = (struct recipe_pack*)item;
        struct oven_pack_options packOptions;

        __initialize_pack_options(&packOptions, recipe, pack, kitchen->install_root);
        status = oven_pack(&packOptions);
        if (status) {
            VLOG_ERROR("bake", "kitchen_recipe_pack: failed to construct pack %s\n", pack->name);
            kitchen_cooking_end(kitchen);
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
