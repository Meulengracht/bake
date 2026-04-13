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
#include <chef/pack.h>
#include <chef/platform.h>
#include <chef/recipe.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vlog.h>

static char* __duplicate_optional_string(const char* string)
{
    if (string == NULL) {
        return NULL;
    }
    return platform_strdup(string);
}

static const char* __resolve_variable(const char* name, void* context)
{
    (void)name;
    (void)context;
    return NULL;
}

static int __read_file_blob(const char* path, struct chef_package_blob* blob)
{
    FILE* file;
    long  fileSize;

    blob->data = NULL;
    blob->size = 0;
    if (path == NULL) {
        return 0;
    }

    file = fopen(path, "rb");
    if (file == NULL) {
        VLOG_ERROR("bake", "failed to open blob file %s\n", path);
        return -1;
    }

    fseek(file, 0, SEEK_END);
    fileSize = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (fileSize <= 0) {
        fclose(file);
        return 0;
    }

    blob->data = malloc((size_t)fileSize);
    if (blob->data == NULL) {
        fclose(file);
        errno = ENOMEM;
        return -1;
    }

    if (fread((void*)blob->data, 1, (size_t)fileSize, file) != (size_t)fileSize) {
        VLOG_ERROR("bake", "failed to read blob file %s\n", path);
        fclose(file);
        free((void*)blob->data);
        blob->data = NULL;
        blob->size = 0;
        return -1;
    }
    fclose(file);
    blob->size = (size_t)fileSize;
    return 0;
}

static int __copy_string_list(struct list* list, struct chef_package_string_array* array)
{
    struct list_item* item;
    size_t            index = 0;

    array->values = NULL;
    array->count = 0;
    if (list == NULL || list->count == 0) {
        return 0;
    }

    array->values = calloc((size_t)list->count, sizeof(char*));
    if (array->values == NULL) {
        errno = ENOMEM;
        return -1;
    }
    array->count = (size_t)list->count;

    list_foreach(list, item) {
        struct list_item_string* value = (struct list_item_string*)item;

        ((char**)array->values)[index] = platform_strdup(value->value);
        if (((char**)array->values)[index] == NULL) {
            errno = ENOMEM;
            return -1;
        }
        index++;
    }
    return 0;
}

static int __build_manifest_commands(
    struct chef_package_manifest* manifest,
    struct recipe_pack*           pack)
{
    struct list_item* item;
    size_t            index = 0;

    if (pack->commands.count == 0) {
        return 0;
    }

    manifest->commands = calloc((size_t)pack->commands.count, sizeof(struct chef_package_manifest_command));
    if (manifest->commands == NULL) {
        errno = ENOMEM;
        return -1;
    }
    manifest->commands_count = (size_t)pack->commands.count;

    list_foreach(&pack->commands, item) {
        struct recipe_pack_command*           command = (struct recipe_pack_command*)item;
        struct chef_package_manifest_command* output = &manifest->commands[index];

        output->type = command->type;
        output->name = __duplicate_optional_string(command->name);
        output->description = __duplicate_optional_string(command->description);
        output->path = __duplicate_optional_string(command->path);
        if (output->name == NULL || output->path == NULL) {
            errno = ENOMEM;
            return -1;
        }

        if (command->arguments.count > 0) {
            output->arguments = chef_process_argument_list(&command->arguments, __resolve_variable, NULL);
            if (output->arguments == NULL) {
                return -1;
            }
        }

        if (__read_file_blob(command->icon, &output->icon) != 0) {
            return -1;
        }
        index++;
    }
    return 0;
}

static int __build_manifest_capabilities(
    struct chef_package_manifest* manifest,
    struct recipe_pack*           pack)
{
    struct list_item* item;
    size_t            index = 0;

    if (pack->capabilities.count == 0) {
        return 0;
    }

    manifest->capabilities = calloc((size_t)pack->capabilities.count, sizeof(struct chef_package_manifest_capability));
    if (manifest->capabilities == NULL) {
        errno = ENOMEM;
        return -1;
    }
    manifest->capabilities_count = (size_t)pack->capabilities.count;

    list_foreach(&pack->capabilities, item) {
        struct recipe_pack_capability*           capability = (struct recipe_pack_capability*)item;
        struct chef_package_manifest_capability* output = &manifest->capabilities[index];

        output->name = __duplicate_optional_string(capability->name);
        if (output->name == NULL) {
            errno = ENOMEM;
            return -1;
        }

        if (strcmp(capability->name, "network-client") == 0) {
            output->type = CHEF_PACKAGE_MANIFEST_CAPABILITY_ALLOW_LIST;
            if (__copy_string_list(&capability->config.network_client.allow, &output->allow_list) != 0) {
                return -1;
            }
        }
        index++;
    }
    return 0;
}

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

static int __build_manifest(
    struct __bake_build_context*  bctx,
    struct recipe_pack*           pack,
    struct chef_package_manifest** manifestOut)
{
    struct chef_package_manifest* manifest;

    manifest = calloc(1, sizeof(struct chef_package_manifest));
    if (manifest == NULL) {
        errno = ENOMEM;
        return -1;
    }

    manifest->name = __duplicate_optional_string(pack->name);
    manifest->platform = __duplicate_optional_string(bctx->target_platform);
    manifest->architecture = __duplicate_optional_string(bctx->target_architecture);
    manifest->base = __duplicate_optional_string(recipe_platform_base(bctx->recipe, bctx->target_platform));
    manifest->summary = __duplicate_optional_string(pack->summary);
    manifest->description = __duplicate_optional_string(pack->description);
    manifest->license = __duplicate_optional_string(bctx->recipe->project.license);
    manifest->eula = __duplicate_optional_string(bctx->recipe->project.eula);
    manifest->maintainer = __duplicate_optional_string(bctx->recipe->project.author);
    manifest->maintainer_email = __duplicate_optional_string(bctx->recipe->project.email);
    manifest->homepage = __duplicate_optional_string(bctx->recipe->project.url);
    manifest->application.network_gateway = __duplicate_optional_string(pack->app_options.gateway);
    manifest->application.network_dns = __duplicate_optional_string(pack->app_options.dns);
    manifest->type = pack->type;

    if (manifest->name == NULL || manifest->platform == NULL || manifest->architecture == NULL) {
        chef_package_manifest_free(manifest);
        errno = ENOMEM;
        return -1;
    }

    if (chef_package_manifest_parse_version(bctx->recipe->project.version, &manifest->version) != 0
     || __read_file_blob(pack->icon, &manifest->icon) != 0
     || __copy_string_list(&pack->options.bin_dirs, &manifest->ingredient.bin_dirs) != 0
     || __copy_string_list(&pack->options.inc_dirs, &manifest->ingredient.inc_dirs) != 0
     || __copy_string_list(&pack->options.lib_dirs, &manifest->ingredient.lib_dirs) != 0
     || __copy_string_list(&pack->options.compiler_flags, &manifest->ingredient.compiler_flags) != 0
     || __copy_string_list(&pack->options.linker_flags, &manifest->ingredient.linker_flags) != 0
     || __build_manifest_commands(manifest, pack) != 0
     || __build_manifest_capabilities(manifest, pack) != 0) {
        chef_package_manifest_free(manifest);
        return -1;
    }

    *manifestOut = manifest;
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
        struct recipe_pack*             pack = (struct recipe_pack*)item;
        struct chef_package_image_options imageOptions;
        struct chef_package_manifest*     manifest = NULL;
        char*                             outputPath = NULL;

        status = __build_manifest(bctx, pack, &manifest);
        if (status == 0) {
            status = __build_output_path(bctx->host_cwd, pack->name, &outputPath);
        }
        if (status == 0) {
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
