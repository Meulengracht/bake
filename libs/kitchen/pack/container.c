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

#include <errno.h>
#include <chef/platform.h>
#include <chef/utils_vafs.h>
#include <chef/recipe.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vafs/vafs.h>
#include <vafs/file.h>
#include <vafs/directory.h>
#include <vlog.h>
#include <zstd.h>
#include "pack.h"
#include "resolvers/resolvers.h"

// include dirent.h for directory operations
#if defined(_WIN32) || defined(_WIN64)
#include <dirent_win32.h>
#else
#include <dirent.h>
#endif

struct progress_context {
    int disabled;

    int files;
    int symlinks;

    int files_total;
    int symlinks_total;
};

struct VaFsFeatureFilter {
    struct VaFsFeatureHeader Header;
};

extern const char* __build_argument_string(struct list* argumentList);

static struct VaFsGuid g_filterGuid    = VA_FS_FEATURE_FILTER;
static struct VaFsGuid g_filterOpsGuid = VA_FS_FEATURE_FILTER_OPS;
static struct VaFsGuid g_headerGuid    = CHEF_PACKAGE_HEADER_GUID;
static struct VaFsGuid g_versionGuid   = CHEF_PACKAGE_VERSION_GUID;
static struct VaFsGuid g_iconGuid      = CHEF_PACKAGE_ICON_GUID;
static struct VaFsGuid g_commandsGuid  = CHEF_PACKAGE_APPS_GUID;
static struct VaFsGuid g_optionsGuid   = CHEF_PACKAGE_INGREDIENT_OPTS_GUID;

/**
 * ZSTD Compression
 * Compression can be between 1-22, which 20+ being extremely consuming.
 * Default compression being (3) => ZSTD_defaultCLevel()
 */
#define __CHEF_ZSTD_COMPRESSION_LEVEL 15

// static context, should be part of something, luckily we don't
// expect parallel packing operations (even though good idea)
static ZSTD_CCtx* g_compressContext = NULL;

static const char* __get_filename(
    const char* path)
{
    const char* filename = (const char*)strrchr(path, CHEF_PATH_SEPARATOR);
    if (filename == NULL) {
        filename = path;
    } else {
        filename++;
    }
    return filename;
}

static int __matches_filters(const char* path, struct list* filters)
{
    struct list_item* item;
    int               status = -1;

    if (filters->count == 0) {
        return 0; // YES! no filters means everything matches
    }

    list_foreach(filters, item) {
        struct list_item_string* filter = (struct list_item_string*)item;
        if (strfilter(filter->value, path, 0) == 0) {
            status = 0;
            break;
        }
    }
    return status;
}

int __get_install_stats(
    struct list* files,
    struct list* filters,
    int*         fileCountOut,
    int*         SymlinkCountOut)
{
    struct list_item* item;

    if (files == NULL || fileCountOut == NULL || SymlinkCountOut == NULL) {
        errno = EINVAL;
        return -1;
    }

    list_foreach(files, item) {
        struct platform_file_entry* entry = (struct platform_file_entry*)item;
        if (__matches_filters(entry->sub_path, filters)) {
            continue;
        }

        switch (entry->type) {
            case PLATFORM_FILETYPE_FILE:
                (*fileCountOut)++;
                break;
            case PLATFORM_FILETYPE_SYMLINK:
                (*SymlinkCountOut)++;
                break;
            default:
                break;
        }
    }
    return 0;
}

static void __write_progress(const char* prefix, struct progress_context* context)
{
    static int last = 0;
    int        current;
    int        total;
    int        percent;

    if (context->disabled) {
        return;
    }

    total   = context->files_total + context->symlinks_total;
    current = context->files + context->symlinks;
    percent = (current * 100) / total;
    if (percent > 100) {
        percent = 100;
    }

    VLOG_TRACE("kitchen", "%3d%% | %s\n", percent, prefix);
}

static int __write_file(
    struct VaFsDirectoryHandle* directoryHandle,
    const char*                 path,
    const char*                 filename,
    uint32_t                    permissions)
{
    struct VaFsFileHandle* fileHandle;
    FILE*                  file;
    long                   fileSize;
    void*                  fileBuffer;
    size_t                 bytesRead;
    int                    status;

    // create the VaFS file
    status = vafs_directory_create_file(directoryHandle, filename, permissions, &fileHandle);
    if (status) {
        return status;
    }

    if ((file = fopen(path, "rb")) == NULL) {
        VLOG_ERROR("kitchen", "unable to open file %s\n", path);
        return -1;
    }

    fseek(file, 0, SEEK_END);
    fileSize = ftell(file);
    if (fileSize) {
        fileBuffer = malloc(fileSize);
        rewind(file);
        bytesRead = fread(fileBuffer, 1, fileSize, file);
        if (bytesRead != fileSize) {
            VLOG_ERROR("kitchen", "only partial read %s\n", path);
        }
        
        // write the file to the VaFS file
        status = vafs_file_write(fileHandle, fileBuffer, fileSize);
        free(fileBuffer);
    }
    fclose(file);

    if (status) {
        VLOG_ERROR("kitchen", "failed to write file '%s': %s\n", filename, strerror(errno));
        return -1;
    }

    status = vafs_file_close(fileHandle);
    if (status) {
        VLOG_ERROR("kitchen", "failed to close file '%s'\n", filename);
        return -1;
    }
    return 0;
}

static int __write_directory(
    struct progress_context*    progress,
    struct list*                filters,
    struct VaFsDirectoryHandle* directoryHandle,
    const char*                 path,
    const char*                 subPath)
{
    struct dirent* dp;
    DIR*           dfd;
    int            status = 0;

    if ((dfd = opendir(path)) == NULL) {
        VLOG_ERROR("kitchen", "can't open initrd folder\n");
        return -1;
    }

    while ((dp = readdir(dfd)) != NULL) {
        struct platform_stat stats;
        const char*          combinedPath;
        const char*          combinedSubPath;

        if (strcmp(dp->d_name, ".") == 0 || strcmp(dp->d_name, "..") == 0) {
            continue;
        }

        combinedPath    = strpathcombine(path, dp->d_name);
        combinedSubPath = strpathcombine(subPath, dp->d_name);
        if (!combinedPath || !combinedSubPath) {
            free((void*)combinedPath);
            break;
        }

        // does this match filters?
        if (__matches_filters(combinedSubPath, filters)) {
            free((void*)combinedPath);
            free((void*)combinedSubPath);
            continue;
        }

        status = platform_stat(combinedPath, &stats);
        if (status) {
            VLOG_ERROR("kitchen", "failed to get filetype for '%s'\n", combinedPath);
            free((void*)combinedPath);
            free((void*)combinedSubPath);
            continue;
        }

        // write progress before to update the file/folder in progress
        __write_progress(dp->d_name, progress);

        // I under normal circumstances do absolutely revolt this type of christmas
        // tree style code, however to avoid 7000 breaks and remembering to cleanup
        // resources, we do it like this. It's not pretty, but it works.
        if (stats.type == PLATFORM_FILETYPE_DIRECTORY) {
            struct VaFsDirectoryHandle* subdirectoryHandle;
            status = vafs_directory_create_directory(directoryHandle, dp->d_name, stats.permissions, &subdirectoryHandle);
            if (status) {
                VLOG_ERROR("kitchen", "failed to create directory '%s'\n", dp->d_name);
            } else {
                status = __write_directory(progress, filters, subdirectoryHandle, combinedPath, combinedSubPath);
                if (status) {
                    VLOG_ERROR("kitchen", "unable to write directory %s\n", combinedPath);
                } else {
                    status = vafs_directory_close(subdirectoryHandle);
                    if (status) {
                        VLOG_ERROR("kitchen", "failed to close directory '%s'\n", combinedPath);
                    }
                }
            }
        } else if (stats.type == PLATFORM_FILETYPE_FILE) {
            status = __write_file(directoryHandle, combinedPath, dp->d_name, stats.permissions);
            if (status) {
                VLOG_ERROR("kitchen", "unable to write file %s\n", dp->d_name);
            }
            progress->files++;
        } else if (stats.type == PLATFORM_FILETYPE_SYMLINK) {
            char* linkpath;
            status = platform_readlink(combinedPath, &linkpath);
            if (status) {
                VLOG_ERROR("kitchen", "failed to read link %s\n", combinedPath);
            } else {
                status = vafs_directory_create_symlink(directoryHandle, dp->d_name, linkpath);
                free(linkpath);
                if (status) {
                    VLOG_ERROR("kitchen", "failed to create symlink %s\n", combinedPath);
                }
            }
            progress->symlinks++;
        } else {
            // ignore unsupported file types
            VLOG_ERROR("kitchen", "unknown filetype for '%s'\n", combinedPath);
            status = 0;
        }

        free((void*)combinedPath);
        free((void*)combinedSubPath);
        if (status) {
            break;
        }

        // write progress after to update the file/folder in progress
        __write_progress(dp->d_name, progress);
    }

    closedir(dfd);
    return status;
}

// TODO on windows this should just put them into same folder as executable
static int __write_syslib(
    struct progress_context*        progress,
    struct VaFsDirectoryHandle*     directoryHandle,
    struct kitchen_resolve_dependency* dependency)
{
    struct VaFsDirectoryHandle* subdirectoryHandle;
    int                         status;

    // write library directories as rwxr-xr-x
    // TODO other platforms
    status = vafs_directory_create_directory(directoryHandle, "lib", 0755, &subdirectoryHandle);
    if (status) {
        VLOG_ERROR("kitchen", "failed to create directory 'lib'\n");
        return status;
    }

    // write libraries as -rw-r--r--
    // TODO other platforms
    status = __write_file(subdirectoryHandle, dependency->path, dependency->name, 0644);
    if (status && errno != EEXIST) {
        VLOG_ERROR("kitchen", "failed to write dependency %s\n", dependency->path);
        return -1;
    }
    progress->files++;

    return vafs_directory_close(subdirectoryHandle);
}

static int __write_filepath(
    struct progress_context*        progress,
    struct VaFsDirectoryHandle*     directoryHandle,
    struct kitchen_resolve_dependency* dependency,
    const char*                     remainingPath)
{
    struct VaFsDirectoryHandle* subdirectoryHandle;
    int                         status;
    char*                       token;
    char*                       remaining;

    // extract next token from the remaining path
    remaining = strchr(remainingPath, CHEF_PATH_SEPARATOR);
    if (!remaining) {
        // write dependencies (libraries) as -rw-r--r--
        // TODO other platforms
        status = __write_file(directoryHandle, dependency->path, dependency->name, 0644);
        if (status && errno != EEXIST) {
            return -1;
        }
        return 0;
    }

    token = platform_strndup(remainingPath, remaining - remainingPath);
    if (!token) {
        return -1;
    }

    // create directory if it doesn't exist
    // write library directories as rwxr-xr-x
    // TODO other platforms
    status = vafs_directory_create_directory(directoryHandle, token, 0755, &subdirectoryHandle);
    free(token);

    if (status) {
        VLOG_ERROR("kitchen", "failed to create directory '%s'\n", token);
        return -1;
    }

    // recurse into the next directory
    status = __write_filepath(progress, subdirectoryHandle, dependency, remaining + 1);
    if (status) {
        VLOG_ERROR("kitchen", "failed to write filepath %s\n", dependency->path);
        return -1;
    }

    return vafs_directory_close(subdirectoryHandle);
}

static int __write_dependencies(
    struct progress_context*    progress,
    struct list*                files,
    struct VaFsDirectoryHandle* directoryHandle)
{
    struct list_item* item;
    int               status;

    list_foreach(files, item) {
        struct kitchen_resolve_dependency* dependency = (struct kitchen_resolve_dependency*)item;
        
        __write_progress(dependency->name, progress);
        if (dependency->system_library) {
            status = __write_syslib(progress, directoryHandle, dependency);
        } else {
            status = __write_filepath(progress, directoryHandle, dependency, dependency->sub_path);
        }

        if (status) {
            VLOG_ERROR("kitchen", "failed to write dependency %s\n", dependency->path);
            return -1;
        }
        __write_progress(dependency->name, progress);
    }
    return 0;
}

static int __zstd_encode(void* Input, uint32_t InputLength, void** Output, uint32_t* OutputLength)
{
    size_t compressedSize = ZSTD_compressBound(InputLength);
    void*  compressedData;
    size_t checkSize;

    compressedData = malloc(compressedSize);
    if (!compressedData) {
        return -1;
    }

    checkSize = ZSTD_compressCCtx(
        g_compressContext,
        compressedData,
        compressedSize,
        Input,
        InputLength,
        __CHEF_ZSTD_COMPRESSION_LEVEL
    );
    if (ZSTD_isError(checkSize)) {
        return -1;
    }

    *Output       = compressedData;
    *OutputLength = checkSize;
    return 0;
}

static int __zstd_decode(void* Input, uint32_t InputLength, void* Output, uint32_t* OutputLength)
{
    /* Read the content size from the frame header. For simplicity we require
     * that it is always present. By default, zstd will write the content size
     * in the header when it is known. If you can't guarantee that the frame
     * content size is always written into the header, either use streaming
     * decompression, or ZSTD_decompressBound().
     */
    size_t             decompressedSize;
    unsigned long long contentSize = ZSTD_getFrameContentSize(Input, InputLength);
    if (contentSize == ZSTD_CONTENTSIZE_ERROR || contentSize == ZSTD_CONTENTSIZE_UNKNOWN) {
        return -1;
    }

    /* Decompress.
     * If you are doing many decompressions, you may want to reuse the context
     * and use ZSTD_decompressDCtx(). If you want to set advanced parameters,
     * use ZSTD_DCtx_setParameter().
     */
    decompressedSize = ZSTD_decompress(Output, *OutputLength, Input, InputLength);
    if (ZSTD_isError(decompressedSize)) {
        return -1;
    }
    *OutputLength = (uint32_t)decompressedSize;
    return 0;
}

static int __set_filter_ops(
    struct VaFs*              vafs,
    struct VaFsFeatureFilter* filter)
{
    struct VaFsFeatureFilterOps filterOps;

    memcpy(&filterOps.Header.Guid, &g_filterOpsGuid, sizeof(struct VaFsGuid));

    filterOps.Header.Length = sizeof(struct VaFsFeatureFilterOps);
    filterOps.Encode = __zstd_encode;
    filterOps.Decode = __zstd_decode;

    return vafs_feature_add(vafs, &filterOps.Header);
}

static int __install_filter(struct VaFs* vafs)
{
    struct VaFsFeatureFilter filter;
    int                      status;

    memcpy(&filter.Header.Guid, &g_filterGuid, sizeof(struct VaFsGuid));
    filter.Header.Length = sizeof(struct VaFsFeatureFilter);
    
    status = vafs_feature_add(vafs, &filter.Header);
    if (status) {
        return status;
    }
    return __set_filter_ops(vafs, &filter);
}

static int __parse_version_string(const char* string, struct chef_vafs_feature_package_version* version)
{
    // parse a version string of format "1.2(+tag)"
    // where tag is optional
    char* pointer    = (char*)string;
    char* pointerEnd = strchr(pointer, '.');
    if (pointerEnd == NULL) {
        return -1;
    }
    
    // extract first part
    version->major = (int)strtol(pointer, &pointerEnd, 10);
    
    // extract second part
    pointer    = pointerEnd + 1;
    pointerEnd = strchr(pointer, '.');
    version->minor = strtol(pointer, &pointerEnd, 10);

    // extract third part if available
    if (pointerEnd != NULL) {
        pointer        = pointerEnd + 1;
        pointerEnd     = NULL;
        version->patch = strtol(pointer, &pointerEnd, 10);
    } else {
        version->patch = 0;
    }

    // default to zero
    version->revision = 0;
    return 0;
}

static int __safe_strlen(const char* string)
{
    if (string == NULL) {
        return 0;
    }
    return strlen(string);
}

static int __write_header_metadata(struct VaFs* vafs, const char* name, struct kitchen_pack_options* options)
{
    struct chef_vafs_feature_package_header* packageHeader;
    size_t                                   featureSize;
    char*                                    dataPointer;
    int                                      status;

    // count up the data requirements for the package header
    featureSize = sizeof(struct chef_vafs_feature_package_header);
    featureSize += strlen(options->platform);
    featureSize += strlen(options->architecture);
    featureSize += strlen(name);
    featureSize += strlen(options->summary);
    featureSize += __safe_strlen(options->description);
    featureSize += __safe_strlen(options->license);
    featureSize += __safe_strlen(options->eula);
    featureSize += __safe_strlen(options->homepage);
    featureSize += strlen(options->maintainer);
    featureSize += strlen(options->maintainer_email);
    
    packageHeader = malloc(featureSize);
    if (!packageHeader) {
        VLOG_ERROR("kitchen", "failed to allocate package header\n");
        return -1;
    }

    memcpy(&packageHeader->header.Guid, &g_headerGuid, sizeof(struct VaFsGuid));
    packageHeader->header.Length = featureSize;

    // fill in info
    packageHeader->version = CHEF_PACKAGE_VERSION;
    packageHeader->type    = options->type;

    // fill in lengths
    packageHeader->platform_length         = strlen(options->platform);
    packageHeader->arch_length             = strlen(options->architecture);
    packageHeader->package_length          = strlen(name);
    packageHeader->summary_length          = strlen(options->summary);
    packageHeader->description_length      = options->description == NULL ? 0 : strlen(options->description);
    packageHeader->license_length          = options->license == NULL ? 0 : strlen(options->license);
    packageHeader->eula_length             = options->eula == NULL ? 0 : strlen(options->eula);
    packageHeader->homepage_length         = options->homepage == NULL ? 0 : strlen(options->homepage);
    packageHeader->maintainer_length       = strlen(options->maintainer);
    packageHeader->maintainer_email_length = strlen(options->maintainer_email);

    // fill in data ptrs
    dataPointer = (char*)packageHeader + sizeof(struct chef_vafs_feature_package_header);

    // required
    memcpy(dataPointer, options->platform, packageHeader->platform_length);
    dataPointer += packageHeader->platform_length;

    // required
    memcpy(dataPointer, options->architecture, packageHeader->arch_length);
    dataPointer += packageHeader->arch_length;

    // required
    memcpy(dataPointer, name, packageHeader->package_length);
    dataPointer += packageHeader->package_length;

#define WRITE_IF_PRESENT(__MEM) if (options->__MEM != NULL) { \
        memcpy(dataPointer, options->__MEM, packageHeader->__MEM ## _length); \
        dataPointer += packageHeader->__MEM ## _length; \
    }

    WRITE_IF_PRESENT(summary)
    WRITE_IF_PRESENT(description)
    WRITE_IF_PRESENT(homepage)
    WRITE_IF_PRESENT(license)
    WRITE_IF_PRESENT(eula)
    WRITE_IF_PRESENT(maintainer)
    WRITE_IF_PRESENT(maintainer_email)

#undef WRITE_IF_PRESENT
    
    // write the package header
    status = vafs_feature_add(vafs, &packageHeader->header);
    free(packageHeader);
    if (status) {
        VLOG_ERROR("kitchen", "failed to write package header\n");
        return -1;
    }
    return status;
}

static int __write_version_metadata(struct VaFs* vafs, const char* version)
{
    struct chef_vafs_feature_package_version* packageVersion;
    size_t                                    featureSize;
    char*                                     tagPointer;
    char*                                     dataPointer;
    int                                       status;

    featureSize = sizeof(struct chef_vafs_feature_package_version);
    tagPointer = strchr(version, '+');
    if (tagPointer != NULL) {
        featureSize += strlen(tagPointer);
    }

    packageVersion = malloc(featureSize);
    if (!packageVersion) {
        VLOG_ERROR("kitchen", "failed to allocate package version\n");
        return -1;
    }

    memcpy(&packageVersion->header.Guid, &g_versionGuid, sizeof(struct VaFsGuid));
    packageVersion->header.Length = featureSize;

    status = __parse_version_string(version, packageVersion);
    if (status) {
        VLOG_ERROR("kitchen", "failed to parse version string %s\n", version);
        return -1;
    }

    packageVersion->tag_length = tagPointer != NULL ? strlen(tagPointer) : 0;

    // fill in data ptrs
    if (tagPointer != NULL) {
        dataPointer = (char*)packageVersion + sizeof(struct chef_vafs_feature_package_version);
        memcpy(dataPointer, tagPointer, packageVersion->tag_length);
        dataPointer += packageVersion->tag_length;
    }

    // write the package header
    status = vafs_feature_add(vafs, &packageVersion->header);
    free(packageVersion);
    return 0;
}

static int __write_icon_metadata(struct VaFs* vafs, const char* path)
{
    struct chef_vafs_feature_package_icon* packageIcon;
    long                                   iconSize;
    char*                                  iconBuffer;
    char*                                  dataPointer;
    int                                    status;
    FILE* 								   file;

    // icon is optional, so just return
    if (path == NULL) {
        return 0;
    }

    file = fopen(path, "rb");
    if (!file) {
        VLOG_ERROR("kitchen", "failed to open icon file %s\n", path);
        return -1;
    }

    fseek(file, 0, SEEK_END);
    iconSize = ftell(file);
    fseek(file, 0, SEEK_SET);
    iconBuffer = malloc(iconSize);
    if (!iconBuffer) {
        VLOG_ERROR("kitchen", "failed to allocate icon buffer\n");
        fclose(file);
        return -1;
    }

    if (fread(iconBuffer, 1, iconSize, file) != iconSize) {
        VLOG_ERROR("kitchen", "failed to read icon file %s\n", path);
        fclose(file);
        free(iconBuffer);
        return -1;
    }
    fclose(file);

    packageIcon = malloc(sizeof(struct chef_vafs_feature_package_icon) + iconSize);
    if (!packageIcon) {
        VLOG_ERROR("kitchen", "failed to allocate package version\n");
        return -1;
    }

    memcpy(&packageIcon->header.Guid, &g_iconGuid, sizeof(struct VaFsGuid));
    packageIcon->header.Length = sizeof(struct chef_vafs_feature_package_icon) + iconSize;

    dataPointer = (char*)packageIcon + sizeof(struct chef_vafs_feature_package_icon);
    memcpy(dataPointer, iconBuffer, iconSize);
    free(iconBuffer);

    // write the package header
    status = vafs_feature_add(vafs, &packageIcon->header);
    free(packageIcon);
    return 0;
}

static size_t __file_size(const char* path)
{
    struct platform_stat fileStat;

    if (path == NULL) {
        return 0;
    }

    if (platform_stat(path, &fileStat) != 0) {
        VLOG_ERROR("kitchen", "failed to stat file %s\n", path);
        return 0;
    }
    return fileStat.size;
}

static size_t __command_size(struct recipe_pack_command* command)
{
    size_t      size = sizeof(struct chef_vafs_package_app);
    const char* args = __build_argument_string(&command->arguments);
    
    size += strlen(command->name);
    size += __safe_strlen(command->description);
    size += __safe_strlen(args);
    size += strlen(command->path);
    size += __file_size(command->icon);
    free((void*)args);
    return size;
}

static size_t __serialize_command(struct recipe_pack_command* command, char* buffer)
{
    struct chef_vafs_package_app* app = (struct chef_vafs_package_app*)buffer;
    const char*                   args = __build_argument_string(&command->arguments);

    app->name_length        = strlen(command->name);
    app->description_length = __safe_strlen(command->description);
    app->arguments_length   = __safe_strlen(args);
    app->type               = (int)command->type;
    app->path_length        = strlen(command->path);
    app->icon_length        = __file_size(command->icon);

    // move the buffer pointer and write in the data
    buffer += sizeof(struct chef_vafs_package_app);
    
    memcpy(buffer, command->name, app->name_length);
    buffer += app->name_length;

    if (command->description) {
        memcpy(buffer, command->description, app->description_length);
        buffer += app->description_length;
    }

    if (command->arguments.count > 0) {
        memcpy(buffer, args, app->arguments_length);
        buffer += app->arguments_length;
    }

    memcpy(buffer, command->path, app->path_length);
    buffer += app->path_length;

    if (app->icon_length > 0) {
        FILE* file = fopen(command->icon, "rb");
        if (file) {
            if (fread(buffer, 1, app->icon_length, file) < app->icon_length) {
                fclose(file);
                return 0;
            }
            fclose(file);
        } else {
            app->icon_length = 0;
        }
    }
    free((void*)args);
    return sizeof(struct chef_vafs_package_app) 
        + app->name_length
        + app->description_length
        + app->arguments_length
        + app->path_length
        + app->icon_length;
}

static int __write_commands_metadata(struct VaFs* vafs, struct list* commands)
{
    struct chef_vafs_feature_package_apps* packageApps;
    struct list_item* item;
    size_t            totalSize = sizeof(struct chef_vafs_feature_package_apps);
    char*             buffer;
    int               status;

    if (commands->count == 0) {
        return 0;
    }

    // start out by counting up the total size the commands will take up when
    // serialized, so we can preallocate the memory
    list_foreach(commands, item) {
        struct recipe_pack_command* command = (struct recipe_pack_command*)item;
        totalSize += __command_size(command);
    }

    buffer = malloc(totalSize);
    if (!buffer) {
        errno = ENOMEM;
        return -1;
    }

    packageApps = (struct chef_vafs_feature_package_apps*)buffer;
    memcpy(&packageApps->header.Guid, &g_commandsGuid, sizeof(struct VaFsGuid));
    packageApps->header.Length = totalSize;
    packageApps->apps_count = commands->count;

    buffer += sizeof(struct chef_vafs_feature_package_apps);
    list_foreach(commands, item) {
        struct recipe_pack_command* command = (struct recipe_pack_command*)item;
        buffer += __serialize_command(command, buffer);
    }

    status = vafs_feature_add(vafs, &packageApps->header);
    free(packageApps);
    return status;
}

static char* __write_list_as_string(struct list* list)
{
    struct list_item* i;
    char*             buffer;
    int               index = 0;

    if (list == NULL || list->count == 0) {
        return NULL;
    }

    // TODO: what should this value be
    buffer = calloc(4096, 1);
    if (buffer == NULL) {
        return NULL;
    }

    list_foreach(list, i) {
        struct list_item_string* str = (struct list_item_string*)i;
        size_t                  len = strlen(str->value);
        if (index != 0) {
            memcpy(&buffer[index++], ",", 1);
        }
        memcpy(&buffer[index], str->value, len);
        index += len;
    }
    return buffer;
}

static int __write_ingredient_options_metadata(struct VaFs* vafs, struct kitchen_pack_options* options)
{
    char  *bins,    *incs,    *libs,    *compiler_flags,    *linker_flags;
    size_t bins_len, incs_len, libs_len, compiler_flags_len, linker_flags_len;
    struct chef_vafs_feature_ingredient_opts* ingOptions;
    size_t totalSize;
    char*  data;
    int    status;

    if (options->type != CHEF_PACKAGE_TYPE_INGREDIENT) {
        return 0;
    }
    
    bins = __write_list_as_string(options->bin_dirs);
    incs = __write_list_as_string(options->inc_dirs);
    libs = __write_list_as_string(options->lib_dirs);
    compiler_flags = __write_list_as_string(options->compiler_flags);
    linker_flags = __write_list_as_string(options->linker_flags);
    
    bins_len = __safe_strlen(bins);
    incs_len = __safe_strlen(incs);
    libs_len = __safe_strlen(libs);
    compiler_flags_len = __safe_strlen(compiler_flags);
    linker_flags_len = __safe_strlen(linker_flags);
    
    totalSize = sizeof(struct chef_vafs_feature_ingredient_opts) + 
        bins_len + incs_len + libs_len + compiler_flags_len + linker_flags_len;
    ingOptions = malloc(totalSize);
    if (ingOptions == NULL) {
        VLOG_ERROR("kitchen", "failed to allocate %u bytes for options metadata\n", totalSize);
        return -1;
    }

    data = (char*)ingOptions + sizeof(struct chef_vafs_feature_ingredient_opts);
#define WRITE_IF_PRESENT(name) if (name ## _len) { memcpy(data, name, name ## _len); data += name ## _len; }

    WRITE_IF_PRESENT(bins)
    WRITE_IF_PRESENT(incs)
    WRITE_IF_PRESENT(libs)
    WRITE_IF_PRESENT(compiler_flags)
    WRITE_IF_PRESENT(linker_flags)

#undef WRITE_IF_PRESENT
    memcpy(&ingOptions->header.Guid, &g_optionsGuid, sizeof(struct VaFsGuid));
    ingOptions->header.Length = totalSize;
    ingOptions->bin_dirs_length = bins_len;
    ingOptions->inc_dirs_length = incs_len;
    ingOptions->lib_dirs_length = libs_len;
    ingOptions->compiler_flags_length = compiler_flags_len;
    ingOptions->linker_flags_length = linker_flags_len;

    status = vafs_feature_add(vafs, &ingOptions->header);
    free(ingOptions);
    return status;
}

static int __write_package_metadata(struct VaFs* vafs, const char* name, struct kitchen_pack_options* options)
{
    int status;

    status = __write_header_metadata(vafs, name, options);
    if (status) {
        VLOG_ERROR("kitchen", "failed to write package header\n");
        return -1;
    }

    status = __write_version_metadata(vafs, options->version);
    if (status) {
        VLOG_ERROR("kitchen", "failed to write package version\n");
        return -1;
    }

    status = __write_icon_metadata(vafs, options->icon);
    if (status) {
        VLOG_ERROR("kitchen", "failed to write package icon\n");
        return -1;
    }

    status = __write_ingredient_options_metadata(vafs, options);
    if (status) {
        VLOG_ERROR("kitchen", "failed to write package ingredient options\n");
        return -1;
    }

    return __write_commands_metadata(vafs, options->commands);
}

static enum VaFsArchitecture __parse_arch(const char* arch)
{
    if (strcmp(arch, "i386") == 0) {
        return VaFsArchitecture_X86;
    } else if (strcmp(arch, "amd64") == 0) {
        return VaFsArchitecture_X64;
    } else if (strcmp(arch, "arm") == 0) {
        return VaFsArchitecture_ARM;
    } else if (strcmp(arch, "arm64") == 0) {
        return VaFsArchitecture_ARM64;
    } else if (strcmp(arch, "riscv32") == 0) {
        return VaFsArchitecture_RISCV32;
    } else if (strcmp(arch, "riscv64") == 0) {
        return VaFsArchitecture_RISCV64;
    }
    return VaFsArchitecture_UNKNOWN;
}

static void __finalize_progress(struct progress_context* progress, const char* packName)
{
    progress->files = progress->files_total;
    progress->symlinks = progress->symlinks_total;
    __write_progress(packName, progress);
}

static int __build_pack_names(const char* name, const char* imageDir, char** basenameOut, char** imagePathOut)
{
    char tmp[4096];

    strbasename(name, &tmp[0], sizeof(tmp));
    *basenameOut = platform_strdup(&tmp[0]);
    if (*basenameOut == NULL) {
        return -1;
    }

    snprintf(&tmp[0], sizeof(tmp), "%s/%s.pack", imageDir, *basenameOut);
    *imagePathOut = platform_strdup(&tmp[0]);
    if (*basenameOut == NULL) {
        free(*basenameOut);
        return -1;
    }
    return 0;
}

static int __is_cross_compiling(const char* target)
{
    // okay so we only care about the target platform here,
    // not the arch
    return strcmp(CHEF_PLATFORM_STR, target) != 0 ? 1 : 0;
}

int kitchen_pack(struct kitchen_pack_options* options)
{
    struct VaFsDirectoryHandle* directoryHandle;
    struct VaFsConfiguration    configuration;
    struct VaFs*                vafs     = NULL;
    struct list                 resolves = { 0 };
    struct list                 files    = { 0 };
    struct list_item*           item;
    struct progress_context     progressContext = { 0 };
    int                         status;
    char*                       name;
    char*                       path;
    VLOG_DEBUG("kitchen", "kitchen_pack(name=%s, path=%s)\n", options->name, options->output_dir);

    if (options == NULL) {
        errno = EINVAL;
        return -1;
    }

    VLOG_DEBUG("kitchen", "enumerating files in %s\n", options->input_dir);
    status = platform_getfiles(options->input_dir, 1, &files);
    if (status) {
        VLOG_ERROR("kitchen", "failed to get files marked for install\n");
        return -1;
    }

    status = __build_pack_names(options->name, options->output_dir, &name, &path);
    if (status) {
        platform_getfiles_destroy(&files);
        VLOG_ERROR("kitchen", "failed to get files marked for install\n");
        return -1;
    }

    __get_install_stats(
        &files,
        options->filters,
        &progressContext.files_total,
        &progressContext.symlinks_total
    );

    // we do not want any empty packs
    if (progressContext.files_total == 0) {
        VLOG_TRACE("kitchen", "skipping pack %s, no files to pack\n", options->name);
        status = 0;
        goto cleanup;
    }

    status = pack_resolve_commands(options->commands, &resolves, &(struct pack_resolve_commands_options) {
        .sysroot = options->sysroot_dir,
        .install_root = options->input_dir,
        .ingredients_root = options->ingredients_root,
        .platform = options->platform,
        .architecture = options->architecture,
        .cross_compiling = __is_cross_compiling(options->platform)
    });
    if (status) {
        VLOG_ERROR("kitchen", "failed to verify commands\n");
        goto cleanup;
    }

    // include all the resolves in the total files count
    list_foreach(&resolves, item) {
        struct kitchen_resolve* resolve = (struct kitchen_resolve*)item;
        progressContext.files_total += resolve->dependencies.count;
    }

    // initialize settings
    vafs_config_initialize(&configuration);
    vafs_config_set_architecture(&configuration, __parse_arch(options->architecture));

    // use 1mb block sizes for container packs
    // TODO: optimally we should select this based on expected container filesize
    // but we tend to produce larger packs atm
    vafs_config_set_block_size(&configuration, 1024 * 1024);

    VLOG_DEBUG("kitchen", "creating %s\n", path);
    status = vafs_create(path, &configuration, &vafs);
    if (status) {
        goto cleanup;
    }

    // Setup compression context
    g_compressContext = ZSTD_createCCtx();
    
    // install the compression for the pack
    status = __install_filter(vafs);
    if (status) {
        VLOG_ERROR("kitchen", "cannot initialize compression\n");
        goto cleanup;
    }

    status = vafs_directory_open(vafs, "/", &directoryHandle);
    if (status) {
        VLOG_ERROR("kitchen", "cannot open root directory\n");
        goto cleanup;
    }


    vlog_set_output_options(stdout, VLOG_OUTPUT_OPTION_RETRACE);
    status = __write_directory(&progressContext, options->filters, directoryHandle, options->input_dir, NULL);
    if (status != 0) {
        VLOG_ERROR("kitchen", "unable to write directory\n");
        goto cleanup;
    }

    list_foreach(&resolves, item) {
        struct kitchen_resolve* resolve = (struct kitchen_resolve*)item;
        status = __write_dependencies(&progressContext, &resolve->dependencies, directoryHandle);
        if (status != 0) {
            VLOG_ERROR("kitchen", "unable to write libraries\n");
            goto cleanup;
        }
    }
    __finalize_progress(&progressContext, name);

    status = __write_package_metadata(vafs, name, options);
    if (status != 0) {
        VLOG_ERROR("kitchen", "unable to write package metadata\n");
    }

cleanup:
    vlog_clear_output_options(stdout, VLOG_OUTPUT_OPTION_RETRACE);
    vafs_close(vafs);
    free(name);
    free(path);
    platform_getfiles_destroy(&files);
    pack_resolve_destroy(&resolves);
    if (g_compressContext != NULL) {
        ZSTD_freeCCtx(g_compressContext);
        g_compressContext = NULL;
    }
    return status;
}
