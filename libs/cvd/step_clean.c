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
#include <chef/dirs.h>
#include <chef/list.h>
#include <chef/platform.h>
#include <chef/recipe.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <vlog.h>

int bake_step_clean(struct __bake_build_context* bctx, struct __build_clean_options* options)
{
    struct list_item* item;
    int               status;
    char              buffer[PATH_MAX];
    unsigned int      pid;
    char*             partName;
    char*             stepName;
    VLOG_DEBUG("bake", "bake_step_clean()\n");

    if (bctx->cvd_client == NULL) {
        errno = ENOTSUP;
        return -1;
    }

    status = recipe_parse_part_step(options->part_or_step, &partName, &stepName);
    if (status) {
        return status;
    }

    if (options->part_or_step != NULL) {
        snprintf(&buffer[0], sizeof(buffer),
            "%s clean --recipe %s --step %s",
            bctx->bakectl_path, bctx->recipe_path, options->part_or_step
        );
    } else {
        snprintf(&buffer[0], sizeof(buffer),
            "%s clean --recipe %s",
            bctx->bakectl_path, bctx->recipe_path
        );
    }

    status = bake_client_spawn(
        bctx,
        &buffer[0],
        CHEF_SPAWN_OPTIONS_WAIT,
        &pid
    );
    if (status) {
        VLOG_ERROR("bake", "failed to perform clean step of '%s'\n", bctx->recipe->project.name);
        return status;
    }

    return status;
}

int bake_purge_kitchens(void)
{
    struct list         recipes;
    struct list_item*   i;
    int                 status;
    const char*         root = chef_dirs_rootfs(NULL);
    VLOG_DEBUG("bake", "bake_purge_kitchens()\n");

    list_init(&recipes);
    status = platform_getfiles(&root[0], 0, &recipes);
    if (status) {
        // ignore this error, just means there is no cleanup to be done
        if (errno != ENOENT) {
            VLOG_ERROR("bake", "bake_purge_kitchens: failed to get current recipes\n");
        }
        goto cleanup;
    }

    list_foreach (&recipes, i) {
        struct platform_file_entry* entry = (struct platform_file_entry*)i;

        VLOG_TRACE("bake", "cleaning %s\n", entry->name);
        status = platform_rmdir(entry->path);
        if (status) {
            VLOG_ERROR("bake", "bake_purge_kitchens: failed to remove data for %s\n", entry->name);
            goto cleanup;
        }
    }

cleanup:
    platform_getfiles_destroy(&recipes);
    return 0;
}
