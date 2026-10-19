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
    const char*  guestResult = "/chef/staging/pack-result.txt";
    char*        temporary = NULL;
    char*        hostResult = NULL;
    void*        result = NULL;
    size_t       resultLength = 0;
    unsigned int pid;
    int          status = -1;
    char*        command;
    const char*  args[11];

    if (__build_output_path("/chef/staging", name, &guestPath) != 0) {
        return -1;
    }

    temporary = platform_tmpdir();
    if (temporary != NULL) {
        hostResult = strpathcombine(temporary, "pack-result.txt");
    }
    if (hostResult == NULL) {
        goto cleanup;
    }

    args[0] = bctx->bakectl_path;
    args[1] = "pack";
    args[2] = "--recipe";
    args[3] = bctx->recipe_path;
    args[4] = "--pack-name";
    args[5] = name;
    args[6] = "--output";
    args[7] = guestPath;
    args[8] = "--result";
    args[9] = guestResult;
    args[10] = NULL;
    
    command = strargv_windows(args);
    if (command == NULL) {
        goto cleanup;
    }

    // Execute the packing
    status = bake_client_spawn(bctx, command, CHEF_SPAWN_OPTIONS_WAIT, &pid);
    free(command);
    if (status == 0) {
        status = bake_client_download(bctx, guestResult, hostResult);
    }
    if (status != 0) {
        status = -1;
        goto cleanup;
    }
    
    status = platform_readfile(hostResult, &result, &resultLength);
    if (status != 0) {
        goto cleanup;
    }
    
    if (resultLength == 8 && memcmp(result, "skipped\n", 8) == 0) {
        status = CHEF_PACKAGE_IMAGE_SKIPPED;
    } else if (resultLength == 8 && memcmp(result, "created\n", 8) == 0) {
        // A missing artifact after an explicit created result is a real error.
        status = bake_client_download(bctx, guestPath, output) == 0 ? 0 : -1;
    } else {
        VLOG_ERROR("bake", "invalid guest packaging result for %s\n", name);
        status = -1;
    }

cleanup:
    if (temporary != NULL) {
        platform_rmdir(temporary);
    }
    free(result);
    free(hostResult);
    free(temporary);
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
        
        // LCOW's install tree lives on guest ext4. The rootfs-provided bakectl
        // packages it there, preserving Linux metadata through the download.
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

        if (status == CHEF_PACKAGE_IMAGE_SKIPPED) {
            // Empty packages are successful skips on both host and guest.
            status = remove(outputPath) == 0 || errno == ENOENT ? 0 : -1;
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
