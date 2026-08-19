/**
 * Copyright 2023, Philip Meulengracht
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

#include <chef/ingredient.h>
#include <chef/package_manifest.h>
#include <chef/platform.h>
#include <chef/vafs.h>
#include <chef/utils_vafs.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vafs/reader.h>
#include <vafs/stat.h>

static struct VaFsGuid g_headerGuid    = CHEF_PACKAGE_HEADER_GUID;
static struct VaFsGuid g_optionsGuid   = CHEF_PACKAGE_INGREDIENT_OPTS_GUID;
static struct VaFsGuid g_overviewGuid  = VA_FS_FEATURE_OVERVIEW;

static int __handle_overview(struct VaFs* vafsHandle, struct ingredient* ingredient)
{
    struct VaFsFeatureOverview* overview;
    int                         status;

    status = vafs_reader_query_feature(vafsHandle, &g_overviewGuid, (struct VaFsFeatureHeader**)&overview);
    if (status) {
        fprintf(stderr, "__handle_overview: failed to query feature overview - %i\n", errno);
        return -1;
    }

    ingredient->file_count      = overview->Counts.Files;
    ingredient->directory_count = overview->Counts.Directories;
    ingredient->symlink_count   = overview->Counts.Symlinks;
    return 0;
}

static int __handle_options(struct VaFs* vafsHandle, struct ingredient* ingredient)
{
    struct chef_vafs_feature_ingredient_opts* options;
    int                                       status;
    char*                                     data;

    // options are optional - ignore if the guid is not present
    status = vafs_reader_query_feature(vafsHandle, &g_optionsGuid, (struct VaFsFeatureHeader**)&options);
    if (status) {
        return 0;
    }

    ingredient->options = malloc(sizeof(struct ingredient_options));
    if (ingredient->options == NULL) {
        return -1;
    }

    data = (char*)options + sizeof(struct chef_vafs_feature_ingredient_opts);

#define READ_IF_PRESENT(__MEM) if (options->__MEM ## _length > 0) { \
        char* line = platform_strndup(data, options->__MEM ## _length); \
        ingredient->options->__MEM = strsplit(line, ','); \
        data += options->__MEM ## _length; \
        free(line); \
    }

    READ_IF_PRESENT(bin_dirs)
    READ_IF_PRESENT(inc_dirs)
    READ_IF_PRESENT(lib_dirs)
    READ_IF_PRESENT(compiler_flags)
    READ_IF_PRESENT(linker_flags)

#undef READ_IF_PRESENT
    return 0;
}

static struct ingredient* __ingredient_new(void)
{
    struct ingredient* ingredient;

    ingredient = calloc(1, sizeof(struct ingredient));
    if (ingredient == NULL) {
        return NULL;
    }
    return ingredient;
}

static struct chef_version* __duplicate_version(const struct chef_version* source)
{
    struct chef_version* version;

    version = calloc(1, sizeof(struct chef_version));
    if (version == NULL) {
        return NULL;
    }

    version->major = source->major;
    version->minor = source->minor;
    version->patch = source->patch;
    version->revision = source->revision;
    version->size = source->size;
    version->created = source->created ? platform_strdup(source->created) : NULL;
    version->tag = source->tag ? platform_strdup(source->tag) : NULL;
    if ((source->created != NULL && version->created == NULL)
     || (source->tag != NULL && version->tag == NULL)) {
        chef_version_free(version);
        return NULL;
    }
    return version;
}

static struct chef_package* __duplicate_package(const struct chef_package_manifest* manifest)
{
    struct chef_package* package;

    package = calloc(1, sizeof(struct chef_package));
    if (package == NULL) {
        return NULL;
    }

    package->platform = manifest->platform ? platform_strdup(manifest->platform) : NULL;
    package->arch = manifest->architecture ? platform_strdup(manifest->architecture) : NULL;
    package->package = manifest->name ? platform_strdup(manifest->name) : NULL;
    package->base = manifest->base ? platform_strdup(manifest->base) : NULL;
    package->summary = manifest->summary ? platform_strdup(manifest->summary) : NULL;
    package->description = manifest->description ? platform_strdup(manifest->description) : NULL;
    package->homepage = manifest->homepage ? platform_strdup(manifest->homepage) : NULL;
    package->license = manifest->license ? platform_strdup(manifest->license) : NULL;
    package->eula = manifest->eula ? platform_strdup(manifest->eula) : NULL;
    package->maintainer = manifest->maintainer ? platform_strdup(manifest->maintainer) : NULL;
    package->maintainer_email = manifest->maintainer_email ? platform_strdup(manifest->maintainer_email) : NULL;
    package->type = manifest->type;
    if ((manifest->platform != NULL && package->platform == NULL)
     || (manifest->architecture != NULL && package->arch == NULL)
     || (manifest->name != NULL && package->package == NULL)
     || (manifest->base != NULL && package->base == NULL)
     || (manifest->summary != NULL && package->summary == NULL)
     || (manifest->description != NULL && package->description == NULL)
     || (manifest->homepage != NULL && package->homepage == NULL)
     || (manifest->license != NULL && package->license == NULL)
     || (manifest->eula != NULL && package->eula == NULL)
     || (manifest->maintainer != NULL && package->maintainer == NULL)
     || (manifest->maintainer_email != NULL && package->maintainer_email == NULL)) {
        chef_package_free(package);
        return NULL;
    }
    return package;
}

static void __ingredient_delete(struct ingredient* ingredient)
{
    if (ingredient == NULL) {
        return;
    }

    chef_package_free(ingredient->package);
    chef_version_free(ingredient->version);
    vafs_directory_reader_close(ingredient->root_reader);
    vafs_reader_close(ingredient->vafs);
    chef_vafs_codec_context_destroy(ingredient->codec_context);
    free(ingredient);
}

int ingredient_open(const char* path, struct ingredient** ingredientOut)
{
    struct VaFs*           vafsHandle;
    struct ingredient*     ingredient;
    struct chef_package_manifest* manifest = NULL;
    int                    status;

    ingredient = __ingredient_new();
    if (ingredient == NULL) {
        return -1;
    }

    status = chef_vafs_codec_context_create(&ingredient->codec_context);
    if (status) {
        fprintf(stderr, "ingredient_open: cannot initialize compression context\n");
        free(ingredient);
        return status;
    }

    status = chef_vafs_reader_open_file(ingredient->codec_context, path, &vafsHandle);
    if (status) {
        fprintf(stderr, "ingredient_open: cannot open vafs image: %s\n", path);
        chef_vafs_codec_context_destroy(ingredient->codec_context);
        free(ingredient);
        return status;
    }
    ingredient->vafs = vafsHandle;

    status = chef_package_manifest_load_vafs(vafsHandle, &manifest);
    if (status) {
        fprintf(stderr, "ingredient_open: cannot open vafs image: %s\n", path);
        __ingredient_delete(ingredient);
        return status;
    }

    ingredient->package = __duplicate_package(manifest);
    ingredient->version = __duplicate_version(&manifest->version);
    if (ingredient->package == NULL || ingredient->version == NULL) {
        chef_package_manifest_free(manifest);
        __ingredient_delete(ingredient);
        errno = ENOMEM;
        return -1;
    }

    status = __handle_overview(vafsHandle, ingredient);
    if (status) {
        fprintf(stderr, "ingredient_open: failed to handle image overview\n");
        __ingredient_delete(ingredient);
        return status;
    }

    status = __handle_options(vafsHandle, ingredient);
    if (status) {
        fprintf(stderr, "ingredient_open: failed to handle ingredient options\n");
        __ingredient_delete(ingredient);
        return status;
    }

    status = vafs_directory_reader_open(vafsHandle, "/", VaFsLookup_None, &ingredient->root_reader);
    if (status) {
        fprintf(stderr, "ingredient_open: cannot open root directory: /\n");
        __ingredient_delete(ingredient);
        return status;
    }

    chef_package_manifest_free(manifest);

    *ingredientOut = ingredient;
    return 0;
}

void ingredient_close(struct ingredient* ingredient)
{
    __ingredient_delete(ingredient);
}

static const char* __get_relative_path(
    const char* root,
    const char* path)
{
    const char* relative = path;
    if (strncmp(path, root, strlen(root)) == 0)
        relative = path + strlen(root);
    return relative;
}

static int __extract_file(
    struct VaFsObjectReader* fileHandle,
    const char*              path)
{
    FILE*                file;
    uint64_t             fileSize;
    uint64_t             bytesRead;
    void*                fileBuffer;
    struct VaFsMetadata  metadata;
    int                  status;

    if ((file = fopen(path, "wb+")) == NULL) {
        fprintf(stderr, "__extract_file: unable to open file %s\n", path);
        return -1;
    }

    fileSize = vafs_object_reader_length(fileHandle);
    if (fileSize) {
        fileBuffer = malloc((size_t)fileSize);
        if (fileBuffer == NULL) {
            fprintf(stderr, "__extract_file: unable to allocate memory for file %s\n", path);
            fclose(file);
            return -1;
        }

        bytesRead = vafs_object_reader_read(fileHandle, fileBuffer, fileSize);
        if (bytesRead != fileSize) {
            free(fileBuffer);
            fclose(file);
            errno = EIO;
            return -1;
        }

        fwrite(fileBuffer, 1, (size_t)fileSize, file);
        free(fileBuffer);
    }
    fclose(file);

    status = vafs_object_reader_stat(fileHandle, &metadata);
    if (status != 0) {
        return status;
    }
    return platform_chmod(path, metadata.Mode & 07777u);
}

static int __extract_directory(
    struct VaFsDirectoryReader* directoryHandle,
    const char*                 root,
    const char*                 path,
    ingredient_progress_cb      progressCB,
    void*                       context)
{
    struct VaFsEntry dp;
    int              status;
    char*            filepathBuffer;

    // ensure the directory exists
    if (strlen(path)) {
        if (platform_mkdir(path)) {
            fprintf(stderr, "__extract_directory: unable to create directory %s\n", path);
            return -1;
        }
    }

    do {
        status = vafs_directory_reader_next(directoryHandle, &dp);
        if (status) {
            if (errno != ENOENT) {
                fprintf(stderr, "__extract_directory: failed to read directory '%s' - %i\n",
                    __get_relative_path(root, path), status);
                return -1;
            }
            break;
        }

        filepathBuffer = strpathcombine(path, dp.Name);
        if (filepathBuffer == NULL) {
            fprintf(stderr, "__extract_directory: unable to allocate memory for filepath\n");
            return -1;
        }

        if (progressCB != NULL) {
            progressCB(dp.Name, INGREDIENT_PROGRESS_START, context);
        }
        if (dp.Type == VaFsEntryType_Directory) {
            struct VaFsDirectoryReader* subdirectoryHandle;

            status = vafs_directory_reader_open_directory_in(directoryHandle, dp.Name, &subdirectoryHandle);
            if (status) {
                fprintf(stderr, "__extract_directory: failed to open directory '%s'\n", __get_relative_path(root, filepathBuffer));
                free(filepathBuffer);
                return -1;
            }

            status = __extract_directory(subdirectoryHandle, root, filepathBuffer, progressCB, context);
            if (status) {
                fprintf(stderr, "__extract_directory: unable to extract directory '%s'\n", __get_relative_path(root, path));
                vafs_directory_reader_close(subdirectoryHandle);
                free(filepathBuffer);
                return -1;
            }

            status = vafs_directory_reader_close(subdirectoryHandle);
            if (status) {
                fprintf(stderr, "__extract_directory: failed to close directory '%s'\n", __get_relative_path(root, filepathBuffer));
                free(filepathBuffer);
                return -1;
            }
            if (progressCB != NULL) {
                progressCB(dp.Name, INGREDIENT_PROGRESS_DIRECTORY, context);
            }
        } else if (dp.Type == VaFsEntryType_File) {
            struct VaFsObjectReader* fileHandle;

            status = vafs_directory_reader_open_object_in(directoryHandle, dp.Name, &fileHandle);
            if (status) {
                fprintf(stderr, "__extract_directory: failed to open file '%s' - %i\n",
                    __get_relative_path(root, filepathBuffer), status);
                free(filepathBuffer);
                return -1;
            }

            status = __extract_file(fileHandle, filepathBuffer);
            if (status) {
                fprintf(stderr, "__extract_directory: unable to extract file '%s'\n", __get_relative_path(root, path));
                vafs_object_reader_close(fileHandle);
                free(filepathBuffer);
                return -1;
            }

            vafs_object_reader_close(fileHandle);
            if (progressCB != NULL) {
                progressCB(dp.Name, INGREDIENT_PROGRESS_FILE, context);
            }
        } else if (dp.Type == VaFsEntryType_Symlink) {
            struct VaFsObjectReader* symlinkHandle;
            uint64_t                 symlinkLength;
            uint64_t                 bytesRead;
            char*                    symlinkTarget;
            
            status = vafs_directory_reader_open_object_in(directoryHandle, dp.Name, &symlinkHandle);
            if (status) {
                fprintf(stderr, "__extract_directory: failed to open symlink '%s' - %i\n",
                    __get_relative_path(root, filepathBuffer), status);
                free(filepathBuffer);
                return -1;
            }

            symlinkLength = vafs_object_reader_length(symlinkHandle);
            symlinkTarget = malloc((size_t)symlinkLength + 1);
            if (symlinkTarget == NULL) {
                vafs_object_reader_close(symlinkHandle);
                free(filepathBuffer);
                errno = ENOMEM;
                return -1;
            }

            bytesRead = vafs_object_reader_read(symlinkHandle, symlinkTarget, symlinkLength);
            vafs_object_reader_close(symlinkHandle);
            if (bytesRead != symlinkLength) {
                free(symlinkTarget);
                free(filepathBuffer);
                errno = EIO;
                return -1;
            }
            symlinkTarget[symlinkLength] = '\0';

            status = platform_symlink(filepathBuffer, symlinkTarget, 0 /* TODO */);
            free(symlinkTarget);
            if (status) {
                fprintf(stderr, "__extract_directory: failed to create symlink '%s' - %i\n",
                    __get_relative_path(root, filepathBuffer), status);
                free(filepathBuffer);
                return -1;
            }
            if (progressCB != NULL) {
                progressCB(dp.Name, INGREDIENT_PROGRESS_SYMLINK, context);
            }
        } else {
            fprintf(stderr, "__extract_directory: unable to extract unknown type '%s'\n", __get_relative_path(root, filepathBuffer));
            free(filepathBuffer);
            return -1;
        }
        free(filepathBuffer);
    } while(1);

    return 0;
}

int ingredient_unpack(struct ingredient* ingredient, const char* path, ingredient_progress_cb progressCB, void* context)
{
    if (ingredient == NULL || path == NULL) {
        errno = EINVAL;
        return -1;
    }
    return __extract_directory(ingredient->root_reader, path, path, progressCB, context);
}
