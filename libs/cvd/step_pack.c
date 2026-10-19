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

#include <chef/cvd.h>
#include <chef/build-common.h>
#include <chef/list.h>
#include <chef/package_image.h>
#include <chef/platform.h>
#include <chef/recipe.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vlog.h>

static int __build_output_path(const char* outputDir, const char* packName, char** pathOut)
{
    char basename[PATH_MAX];
    char buffer[PATH_MAX];

    strbasename(packName, &basename[0], sizeof(basename));
    snprintf(&buffer[0], sizeof(buffer), "%s/%s.pack", outputDir, &basename[0]);
    *pathOut = platform_strdup(&buffer[0]);
    if (*pathOut == NULL) {
        errno = ENOMEM;
        return -1;
    }
    return 0;
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

static int __pack_in_guest(struct __bake_build_context* bctx, const char* name, const char* output)
{
    char*        guestPath = NULL;
    unsigned int pid;
    int          status;
    char*        command;
    const char*  args[9];

    if (__build_output_path("/chef/staging", name, &guestPath) != 0) {
        return -1;
    }

    args[0] = bctx->bakectl_path;
    args[1] = "pack";
    args[2] = "--recipe";
    args[3] = bctx->recipe_path;
    args[4] = "--pack-name";
    args[5] = name;
    args[6] = "--output";
    args[7] = guestPath;
    args[8] = NULL;
    
    command = strargv_windows(args);
    if (command == NULL) {
        free(guestPath);
        return -1;
    }

    // Execute the packing
    status = bake_client_spawn(bctx, command, CHEF_SPAWN_OPTIONS_WAIT, &pid);
    free(command);
    if (status == 0) {
        // Download the packed file from the guest to the host.
        status = bake_client_download(bctx, guestPath, output);
    }
    free(guestPath);
    return status;
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
        struct recipe_pack*               pack = (struct recipe_pack*)item;
        struct chef_package_image_options imageOptions;
        struct chef_package_manifest*     manifest = NULL;
        char*                             outputPath = NULL;

        status = __build_output_path(bctx->host_cwd, pack->name, &outputPath);
        
        // Because the build's actual install tree now lives on the guest’s ext4 disk. 
        // Windows can't directly read that tree, and the old host packaging path would read the 
        // original seed directory—which doesn't contain the build output.
        // Copying the install tree back through Plan9/NTFS would risk losing the Linux symlinks 
        // and permissions we introduced ext4 to preserve. Instead, the rootfs-provided bakectl 
        // reads the native install tree and creates the .pack inside LCOW. Windows then downloads that file.
        // This applies only to Windows-hosted Linux builds. Native Linux and Windows-container builds retain host-side packaging.
#ifdef _WIN32
        if (status == 0 && bctx->guest_type == CHEF_GUEST_TYPE_LINUX) {
            status = __pack_in_guest(bctx, pack->name, outputPath);
        } else
#endif
        if (status == 0) {
            status = chef_package_manifest_from_recipe(
                bctx->recipe,
                bctx->target_platform,
                bctx->target_architecture,
                pack,
                &manifest
            );
            if (status != 0) {
                free(outputPath);
                goto cleanup;
            }
            
            imageOptions.input_dir = bctx->install_path;
            imageOptions.output_path = outputPath;
            imageOptions.filters = &pack->filters;
            imageOptions.manifest = manifest;
            status = chef_package_image_create(&imageOptions);
        }

        chef_package_manifest_free(manifest);
        free(outputPath);
        if (status) {
            VLOG_ERROR("bake", "kitchen_recipe_pack: failed to construct pack %s\n", pack->name);
            goto cleanup;
        }
    }

cleanup:
    return status;
}
