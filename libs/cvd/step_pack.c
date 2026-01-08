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

#include <chef/cvd.h>
#include <chef/list.h>
#include <chef/pack.h>
#include <chef/platform.h>
#include <chef/recipe.h>
#include <errno.h>
#include <stdlib.h>
#include <vlog.h>

static void __initialize_pack_options(
    struct __bake_build_context* bctx,
    struct __pack_options*       options,
    struct recipe_pack*          pack)
{
    memset(options, 0, sizeof(struct __pack_options));
    options->name             = pack->name;
    options->output_dir       = bctx->host_cwd;
    options->input_dir        = bctx->install_path;
    options->platform         = bctx->target_platform;
    options->architecture     = bctx->target_architecture;

    options->type             = pack->type;
    options->base             = recipe_platform_base(bctx->recipe, bctx->target_platform);
    options->summary          = pack->summary;
    options->description      = pack->description;
    options->icon             = pack->icon;
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

static int __stage_ingredients(struct __bake_build_context* bctx)
{
    char         buffer[PATH_MAX];
    unsigned int pid;

    if (bctx->cvd_client == NULL) {
        errno = ENOTSUP;
        return -1;
    }
    
    snprintf(&buffer[0], sizeof(buffer),
        "%s stage --recipe %s",
        bctx->bakectl_path, bctx->recipe_path
    );
    
    return bake_client_spawn(
        bctx,
        &buffer[0],
        CHEF_SPAWN_OPTIONS_WAIT,
        &pid
    );
}

int build_step_pack(struct __bake_build_context* bctx)
{
    struct list_item* item;
    int               status;
    VLOG_DEBUG("bake", "kitchen_recipe_pack()\n");

    // stage before we pack
    status = __stage_ingredients(bctx);
    if (status) {
        VLOG_ERROR("bake", "failed to perform stage step of '%s'\n", bctx->recipe->project.name);
        return status;
    }

    list_foreach(&bctx->recipe->packs, item) {
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
