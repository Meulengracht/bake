/**
 * Copyright 2022, Philip Meulengracht
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

#include <backend.h>
#include <errno.h>
#include <libingredient.h>
#include <liboven.h>
#include <chef/platform.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <utils.h>
#include <vafs/vafs.h>
#include <zip.h>

// import this while we find a suitiable place
extern const char* oven_preprocess_text(const char* original);

static char* __processed_path(struct oven_backend_data* data)
{
    return strpathcombine(data->paths.build, "cross-file.txt");
}

static char* __wrap_file_path(struct oven_backend_data* data, struct meson_wrap_item* item)
{
    char* buffer;

    buffer = malloc(4096);
    if (buffer == NULL) {
        return NULL;
    }

    snprintf(buffer, 4096 - 1, 
        "%s" CHEF_PATH_SEPARATOR_S "subprojects" 
             CHEF_PATH_SEPARATOR_S "%s.wrap",
        data->paths.project, item->name
    );
    return buffer;
}

static char* __meson_project_path(struct oven_backend_data* data, struct meson_wrap_item* item)
{
    char* buffer;

    buffer = malloc(4096);
    if (buffer == NULL) {
        return NULL;
    }

    snprintf(buffer, 4096 - 1, 
        "%s" CHEF_PATH_SEPARATOR_S "subprojects" 
             CHEF_PATH_SEPARATOR_S "packagefiles"
             CHEF_PATH_SEPARATOR_S "%s"
             CHEF_PATH_SEPARATOR_S "meson.build",
        data->paths.project, item->name
    );
    return buffer;
}

static char* __zip_path(struct oven_backend_data* data, struct meson_wrap_item* item, struct oven_ingredient* ingredient)
{
    char* buffer;

    buffer = malloc(4096);
    if (buffer == NULL) {
        return NULL;
    }

    snprintf(buffer, 4096 - 1, 
        "%s" CHEF_PATH_SEPARATOR_S "subprojects" 
             CHEF_PATH_SEPARATOR_S "packagefiles"
             CHEF_PATH_SEPARATOR_S "%s"
             CHEF_PATH_SEPARATOR_S "pack-%i.%i.%i.zip",
        data->paths.project, item->name, ingredient->version->major, ingredient->version->minor, ingredient->version->patch
    );
    return buffer;
}

static int __read_file(const char* path, char** bufferOut)
{
    FILE* file;
    long  size;
    char* buffer;

    file = fopen(path, "rb");
    if (file == NULL) {
        fprintf(stderr, "Failed to open %s for reading: %s\n", path, strerror(errno));
        return -1;
    }

    fseek(file, 0, SEEK_END);
    size = ftell(file);
    rewind(file);

    buffer = malloc(size);
    if (buffer == NULL) {
        fprintf(stderr, "Failed to read %s: %s\n", path, strerror(errno));
        fclose(file);
        return -1;
    }

    fread(buffer, 1, size, file);
    fclose(file);
    return 0;
}

static int __write_file(const char* path, const char* buffer)
{
    FILE* file;
    int   status;

    file = fopen(path, "w");
    if (file == NULL) {
        fprintf(stderr, "Failed to open %s for writing: %s\n", path, strerror(errno));
        free(path);
        return -1;
    }

    fputs(buffer, file);
    fclose(file);
    return 0;
}

static char* __compute_arguments(struct oven_backend_data* data, union oven_backend_options* options)
{
    size_t length;
    char*  args;
    int    status;
    FILE*  stream;

    stream = open_memstream(&args, &length);
    if (stream == NULL) {
        return NULL;
    }

    if (options->meson.cross_file != NULL) {
        char* original, *processed, *path;
        
        path = __processed_path(data);
        if (path == NULL) {
            return NULL;
        }

        /**
         * @brief The cross-file we take in, is a template. We will be pre-processing it a bit
         * before writing a final cross-file to handle any variables present.
         */
        status = __read_file(options->meson.cross_file, &original);
        if (status) {
            free(path);
            return NULL;
        }

        processed = oven_preprocess_text(original);
        if (processed == NULL) {
            free(path);
            free(original);
            return NULL;
        }

        status = __write_file(path, processed);
        free(original);
        free(processed);

        if (status) {
            free(path);
            return NULL;
        }
        fprintf(stream, "--cross-file %s", path);
        free(path);
    }

    // --force-fallback-for=llvm

    fclose(stream);
    return args;
}

/**
 * @brief Create the following paths, and ensure there is a backup of any pre-existing wrap file
 * ${{PROJECT_DIR}}/subprojects/packagefiles/${depname}
 * ${{PROJECT_DIR}}/subprojects/${depname}.wrap
 */
static int __initiate_meson_dep(struct oven_backend_data* data, struct meson_wrap_item* item)
{

}

static struct oven_ingredient* __find_ingredient(struct oven_backend_data* data, const char* name)
{
    struct list_item* i;
    if (data->ingredients == NULL) {
        return NULL;
    }

    list_foreach(data->ingredients, i) {
        struct oven_ingredient* ing = (struct oven_ingredient*)i;
        if (strcmp(ing->name, name) == 0) {
            return ing;
        }
    }
    return NULL;
}

static int __file_exists(const char* path)
{
    FILE* file = fopen(path, "r");
    if (file == NULL) {
        return -1;
    }
    return fclose(file);
}

static int __zip_entry(struct zip_t* zip, const char* ingredientPrefix, const char* path)
{
    int   status;
    char* filePath;

    filePath = strpathcombine(ingredientPrefix, path);
    if (filePath == NULL) {
        return -1;
    }

    status = zip_entry_open(zip, path);
    if (status) {
        fprintf(stderr, "__zip_file: unable to create entry %s in zip archieve\n", path);
        free(filePath);
        return -1;
    }

    status = zip_entry_fwrite(zip, filePath);
    if (status) {
        fprintf(stderr, "__zip_file: unable to write %s to zip archieve\n", path);
    }
    zip_entry_close(zip);
    free(filePath);
    return status;
}

static int __repack_directory(struct zip_t* zip, const char* ingredientPrefix, struct VaFsDirectoryHandle* directoryHandle, const char* path)
{

    struct VaFsEntry dp;
    int              status;
    char*            filepathBuffer;

    do {
        status = vafs_directory_read(directoryHandle, &dp);
        if (status) {
            if (errno != ENOENT) {
                fprintf(stderr, "__repack_directory: failed to read directory '%s' - %i\n", path, status);
                return -1;
            }
            break;
        }

        filepathBuffer = strpathcombine(path, dp.Name);
        if (filepathBuffer == NULL) {
            fprintf(stderr, "__repack_directory: unable to allocate memory for filepath\n");
            return -1;
        }

        if (dp.Type == VaFsEntryType_Directory) {
            struct VaFsDirectoryHandle* subdirectoryHandle;
            status = vafs_directory_open_directory(directoryHandle, dp.Name, &subdirectoryHandle);
            if (status) {
                fprintf(stderr, "__repack_directory: failed to open directory '%s'\n", filepathBuffer);
                return -1;
            }

            status = __repack_directory(zip, ingredientPrefix, subdirectoryHandle, filepathBuffer);
            if (status) {
                fprintf(stderr, "__repack_directory: unable to extract directory '%s'\n", path);
                return -1;
            }

            status = vafs_directory_close(subdirectoryHandle);
            if (status) {
                fprintf(stderr, "__repack_directory: failed to close directory '%s'\n", filepathBuffer);
                return -1;
            }
        } else if (dp.Type == VaFsEntryType_File || dp.Type == VaFsEntryType_Symlink) {
            status = __zip_entry(zip, ingredientPrefix, filepathBuffer);
            if (status) {
                fprintf(stderr, "__repack_directory: unable to extract file '%s'\n", path);
                return -1;
            }
        } else {
            fprintf(stderr, "__repack_directory: unable to extract unknown type '%s'\n", filepathBuffer);
            return -1;
        }
        free(filepathBuffer);
    } while(1);

    return 0;
}

/**
 * @brief Repack a chef ingredient as a meson project zip. This is required
 * to support the external dependency feature of meson. We repack them and place
 * them into ${{PROJECT_DIR}}/subprojects/packagefiles/${depname}/pack-${version}.zip
 */
static int __repack_ingredient(struct oven_backend_data* data, struct meson_wrap_item* item)
{
    struct oven_ingredient* ovenIngredient;
    struct zip_t*           zip;
    char*                   zip_path;
    struct ingredient*      ingredient;
    int                     status;
    
    ovenIngredient = __find_ingredient(data, item->ingredient);
    if (ovenIngredient == NULL) {
        errno = ENOENT;
        return -1;
    }

    zip_path = __zip_path(data, item, ovenIngredient);
    if (zip_path == NULL) {
        return -1;
    }

    if (!__file_exists(zip_path)) {
        free(zip_path);
        return 0;
    }

    status = ingredient_open(ovenIngredient->file_path, &ingredient);
    if (status) {
        free(zip_path);
        return -1;
    }

    zip = zip_open(zip_path, ZIP_DEFAULT_COMPRESSION_LEVEL, 'w');
    if (zip == NULL) {
        ingredient_close(ingredient);
        free(zip_path);
        return -1;
    }

    status = __repack_directory(zip, data->paths.ingredients, ingredient->root_handle, "/");

    zip_close(zip);
    ingredient_close(ingredient);
    free(zip_path);
    return status;
}

/**
 * @brief Project file must be written into ${{PROJECT_DIR}}/subprojects/packagefiles/${depname}/meson.build
 * 
 */
static int __write_project_file(struct oven_backend_data* data, struct meson_wrap_item* item)
{
    char* filePath;
    FILE* file;

    filePath = __meson_project_path(data, item);
    if (filePath == NULL) {
        return -1;
    }

    file = fopen(filePath, "w");
    if (file == NULL) {
        free(filePath);
        return -1;
    }

    /**
     * @brief Example of a project file
     * project('bob', 'c')
     * 
     * # Do some sanity checking so that meson can fail early instead of at final link time
     * if not (host_machine.system() == 'windows' and host_machine.cpu_family() == 'x86_64')
     * error('This wrap of libbob is a binary wrap for x64_64 Windows, and will not work on your system')
     * endif
     * 
     * cc = meson.get_compiler('c')
     * bob_dep = declare_dependency(
     * dependencies : cc.find_library('bob', dirs : meson.current_source_dir()),
     * include_directories : include_directories('include'))
     * 
     * meson.override_dependency('bob', bob_dep)
     */
    fprintf(file, "project(%s, ['c', 'cpp'])\n\n", item->name);
    fprintf(file, "# add c compiler magic\n");
    fprintf(file, "cc = meson.get_compiler('c')\n");
    fprintf(file, "%s_dep = declare_dependecy(\n", item->name);
    fprintf(file, "    dependencies : cc.find_library('%s', dirs : meson.current_source_dir()),\n", item->name);

    free(filePath);
    return 0;
}

/**
 * @brief Wrap files must be written into ${{PROJECT_DIR}}/subprojects/{name}.wrap
 * 
 */
static int __write_wrap_file(struct oven_backend_data* data, struct meson_wrap_item* item)
{
    char* filePath;
    FILE* file;

    filePath = __wrap_file_path(data, item);
    if (filePath == NULL) {
        return -1;
    }

    file = fopen(filePath, "w");
    if (file == NULL) {
        free(filePath);
        return -1;
    }

    /**
     * @brief Example of a wrap file that we must write
     * [wrap-file]
     *  source_filename = ${{}}/${}.zip
     *  patch_directory = libbob
     * 
     *  [provide]
     *  dependency_names = bob
     */

    free(filePath);
    return 0;
}

/**
 * @brief To support automatical import of dependencies for a meson based project
 * we must support the wrap system. Unfortunately the wrap system supports a different
 * way of packing dependencies, so we must do some magic to create some binary packages
 * from the ingredient system, and then JIT drop them in.
 * - https://mesonbuild.com/Wrap-dependency-system-manual.html
 * - https://mesonbuild.com/Shipping-prebuilt-binaries-as-wraps.html
 * 
 */
static int __handle_wrap_items(struct oven_backend_data* data, union oven_backend_options* options)
{
    struct list_item* i;

    list_foreach(&options->meson.wraps, i) {
        struct meson_wrap_item* item = (struct meson_wrap_item*)i;
        int                     status;

        status = __initiate_meson_dep(data, item);
        if (status) {
            return -1;
        }

        status = __repack_ingredient(data, item);
        if (status) {
            return -1;
        }

        status = __write_project_file(data, item);
        if (status) {
            return -1;
        }

        status = __write_wrap_file(data, item);
        if (status) {
            return -1;
        }
    }
    return 0;
}

int meson_config_main(struct oven_backend_data* data, union oven_backend_options* options)
{
    char*  mesonCommand = NULL;
    char** environment  = NULL;
    char*  args         = NULL;
    int    status = -1;
    size_t length;

    status = __handle_wrap_items(data, options);
    if (status) {
        return -1;
    }

    environment = oven_environment_create(data->process_environment, data->environment);
    if (environment == NULL) {
        errno = ENOMEM;
        return -1;
    }

    // lets make it 64 to cover some extra grounds
    length = 64 + strlen(data->paths.project);
    args = __compute_arguments(data, options);
    if (args) {
        length += strlen(args);
    }
    mesonCommand = malloc(args);
    if (mesonCommand == NULL) {
        errno = ENOMEM;
        goto cleanup;
    }

    if (args) {
        sprintf(mesonCommand, "meson setup %s %s", args, data->paths.project);
    } else {
        sprintf(mesonCommand, "meson setup %s", data->paths.project);
    }

    // use the project directory (cwd) as the current build directory
    status = platform_spawn(
        mesonCommand,
        data->arguments,
        (const char* const*)environment,
        data->paths.build
    );

cleanup:
    free(args);
    free(mesonCommand);
    oven_environment_destroy(environment);
    return status;
}
