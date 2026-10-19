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

#include <chef/package_image.h>
#include <chef/platform.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "commands.h"

int pack_main(int argc, char** argv, struct __bakelib_context* context, struct bakectl_command_options* options)
{
    const char*                       name = NULL;
    const char*                       output = NULL;
    const char*                       resultPath = NULL;
    struct recipe_pack*               pack = NULL;
    struct list_item*                 item;
    struct chef_package_manifest*     manifest = NULL;
    struct chef_package_image_options image = { 0 };
    int                               status;
    (void)options;

    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--output") && i + 1 < argc) {
            output = argv[++i];
        } else if (!strcmp(argv[i], "--pack-name") && i + 1 < argc) {
            name = argv[++i];
        } else if (!strcmp(argv[i], "--result") && i + 1 < argc) {
            resultPath = argv[++i];
        }
    }

    if (output == NULL || name == NULL || name[0] == '\0') {
        fprintf(stderr, "bakectl pack: --output and --pack-name are required\n");
        return -1;
    }

    list_foreach(&context->recipe->packs, item) {
        struct recipe_pack* candidate = (struct recipe_pack*)item;
        if (strcmp(candidate->name, name) == 0) {
            if (pack != NULL) {
                fprintf(stderr, "bakectl pack: duplicate package name '%s'\n", name);
                return -1;
            }
            pack = candidate;
        }
    }

    if (pack == NULL) {
        fprintf(stderr, "bakectl pack: package '%s' not found in recipe\n", name);
        return -1;
    }

    if (resultPath != NULL) {
        if (strcmp(resultPath, output) == 0) {
            fprintf(stderr, "bakectl pack: result and package paths must differ\n");
            return -1;
        }
        
        if (remove(resultPath) != 0 && errno != ENOENT) {
            return -1;
        }
    }
    
    // Do not let a skipped/failed pack expose a previous build's artifact.
    if (remove(output) != 0 && errno != ENOENT) {
        fprintf(stderr, "bakectl pack: failed to remove existing output file '%s': %s\n", output, strerror(errno));
        return -1;
    }

    status = chef_package_manifest_from_recipe(
        context->recipe,
        context->build_platform,
        context->build_architecture,
        pack,
        &manifest
    );
    if (status) {
        goto cleanup;
    }
    
    image.input_dir = context->install_directory;
    image.output_path = output;
    image.filters = &pack->filters;
    image.manifest = manifest;
    status = chef_package_image_create(&image);
    if (status == CHEF_PACKAGE_IMAGE_CREATED || status == CHEF_PACKAGE_IMAGE_SKIPPED) {
        const char* result = status == CHEF_PACKAGE_IMAGE_SKIPPED ? "skipped\n" : "created\n";
        status = resultPath != NULL ? platform_writetextfile(resultPath, result) : 0;
    }

cleanup:
    chef_package_manifest_free(manifest);
    if (status != 0) {
        remove(output);
        if (resultPath != NULL) remove(resultPath);
    }
    return status;
}
