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
#include <chef/utils_vafs.h>
#include <liboven.h>
#include <libplatform.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vafs/vafs.h>
#include <zstd.h>

// include dirent.h for directory operations
#if defined(_WIN32) || defined(_WIN64)
#include <dirent_win32.h>
#else
#include <dirent.h>
#endif

struct progress_context {
    int disabled;

    int files;
    int directories;
    int symlinks;

    int files_total;
    int directories_total;
    int symlinks_total;
};

struct VaFsFeatureFilter {
    struct VaFsFeatureHeader Header;
};

extern const char* __get_install_path(void);

static struct VaFsGuid g_filterGuid    = VA_FS_FEATURE_FILTER;
static struct VaFsGuid g_filterOpsGuid = VA_FS_FEATURE_FILTER_OPS;
static struct VaFsGuid g_headerGuid    = CHEF_PACKAGE_HEADER_GUID;
static struct VaFsGuid g_versionGuid   = CHEF_PACKAGE_VERSION_GUID;
static struct VaFsGuid g_iconGuid      = CHEF_PACKAGE_ICON_GUID;
static struct VaFsGuid g_commandsGuid  = CHEF_PACKAGE_APPS_GUID;

static const char* __get_filename(
    const char* path)
{
    const char* filename = (const char*)strrchr(path, '/');
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

    if (filters->count == 0) {
        return 0; // YES! no filters means everything matches
    }

    list_foreach(filters, item) {
        struct oven_value_item* filter = (struct oven_value_item*)item;
        if (strfilter(filter->value, path, 0) != 0) {
            return -1;
        }
    }
    return 0;
}

int __get_count_recursive(
    const char*  path,
    const char*  subPath,
    struct list* filters,
    int*         fileCountOut,
    int*         SymlinkCountOut,
    int*         dirCountOut)
{
    struct dirent* dp;
    DIR*           d;
    int            status = 0;

    if (!path) {
        errno = EINVAL;
        return -1;
    }

    if ((d = opendir(path)) == NULL) {
        return -1;
    }

    while ((dp = readdir(d))) {
        char* combinedPath;
        char* combinedSubPath;

        if (strcmp(dp->d_name,".") == 0 || strcmp(dp->d_name,"..") == 0) {
             continue;
        }

        combinedPath    = strpathcombine(path, dp->d_name);
        combinedSubPath = strpathcombine(subPath, dp->d_name);
        if (!combinedPath || !combinedSubPath) {
            free((void*)combinedPath);
            break;
        }

        // does this match filters?
        status = __matches_filters(combinedSubPath, filters);
        if (status) {
            free((void*)combinedPath);
            free((void*)combinedSubPath);
            continue;
        }

        switch (dp->d_type) {
            case DT_REG:
                (*fileCountOut)++;
                break;
            case DT_DIR: {
                (*dirCountOut)++;
                status = __get_count_recursive(combinedPath, combinedSubPath, filters, fileCountOut, SymlinkCountOut, dirCountOut);
            } break;
            case DT_LNK:
                (*SymlinkCountOut)++;
                break;
            default:
                break;
        }

        free((void*)combinedPath);
        free((void*)combinedSubPath);
        if (status) {
            break;
        }
    }
    closedir(d);
    return status;
}

static void __write_progress(const char* prefix, struct progress_context* context, int verbose)
{
    static int last = 0;
    int        current;
    int        total;
    int        percent;

    if (context->disabled) {
        return;
    }

    total   = context->files_total + context->directories_total + context->symlinks_total;
    current = context->files + context->directories + context->symlinks;
    percent = (current * 100) / total;

    printf("\33[2K\r%-10.10s [", prefix);
    for (int i = 0; i < 20; i++) {
        if (i < percent / 5) {
            printf("#");
        }
        else {
            printf(" ");
        }
    }
    printf("| %3d%%]", percent);
    if (verbose) {
        if (context->files_total) {
            printf(" %i/%i files", context->files, context->files_total);
        }
        if (context->directories_total) {
            printf(" %i/%i directories", context->directories, context->directories_total);
        }
        if (context->symlinks_total) {
            printf(" %i/%i symlinks", context->symlinks, context->symlinks_total);
        }
    }
    fflush(stdout);
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
        fprintf(stderr, "oven: failed to create file '%s'\n", filename);
        return -1;
    }

    if ((file = fopen(path, "rb")) == NULL) {
        fprintf(stderr, "oven: unable to open file %s\n", path);
        return -1;
    }

    fseek(file, 0, SEEK_END);
    fileSize = ftell(file);
    if (fileSize) {
        fileBuffer = malloc(fileSize);
        rewind(file);
        bytesRead = fread(fileBuffer, 1, fileSize, file);
        if (bytesRead != fileSize) {
            fprintf(stderr, "oven: only partial read %s\n", path);
        }
        
        // write the file to the VaFS file
        status = vafs_file_write(fileHandle, fileBuffer, fileSize);
        free(fileBuffer);
    }
    fclose(file);

    if (status) {
        fprintf(stderr, "oven: failed to write file '%s'\n", filename);
        return -1;
    }

    status = vafs_file_close(fileHandle);
    if (status) {
        fprintf(stderr, "oven: failed to close file '%s'\n", filename);
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
        fprintf(stderr, "oven: can't open initrd folder\n");
        return -1;
    }

    while ((dp = readdir(dfd)) != NULL) {
        enum platform_filetype fileType;
        uint32_t               filePermissions;
        const char*            combinedPath;
        const char*            combinedSubPath;

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

        status = platform_stat(combinedPath, &fileType, &filePermissions);
        if (status) {
            fprintf(stderr, "oven: failed to get filetype for '%s'\n", combinedPath);
            free((void*)combinedPath);
            free((void*)combinedSubPath);
            continue;
        }

        // write progress before to update the file/folder in progress
        __write_progress(dp->d_name, progress, 0);

        // I under normal circumstances do absolutely revolt this type of christmas
        // tree style code, however to avoid 7000 breaks and remembering to cleanup
        // resources, we do it like this. It's not pretty, but it works.
        if (fileType == PLATFORM_FILETYPE_DIRECTORY) {
            struct VaFsDirectoryHandle* subdirectoryHandle;
            status = vafs_directory_create_directory(directoryHandle, dp->d_name, filePermissions, &subdirectoryHandle);
            if (status) {
                fprintf(stderr, "oven: failed to create directory '%s'\n", dp->d_name);
            } else {
                status = __write_directory(progress, filters, subdirectoryHandle, combinedPath, combinedSubPath);
                if (status) {
                    fprintf(stderr, "oven: unable to write directory %s\n", combinedPath);
                } else {
                    status = vafs_directory_close(subdirectoryHandle);
                    if (status) {
                        fprintf(stderr, "oven: failed to close directory '%s'\n", combinedPath);
                    }
                }
            }
            progress->directories++;
        } else if (fileType == PLATFORM_FILETYPE_FILE) {
            status = __write_file(directoryHandle, combinedPath, dp->d_name, filePermissions);
            if (status) {
                fprintf(stderr, "oven: unable to write file %s\n", dp->d_name);
            }
            progress->files++;
        } else if (fileType == PLATFORM_FILETYPE_SYMLINK) {
            char* linkpath;
            status = platform_readlink(combinedPath, &linkpath);
            if (status) {
                fprintf(stderr, "oven: failed to read link %s\n", combinedPath);
            } else {
                status = vafs_directory_create_symlink(directoryHandle, dp->d_name, linkpath);
                free(linkpath);
                if (status) {
                    fprintf(stderr, "oven: failed to create symlink %s\n", combinedPath);
                }
            }
            progress->symlinks++;
        } else {
            // ignore unsupported file types
            fprintf(stderr, "oven: unknown filetype for '%s'\n", combinedPath);
            status = 0;
        }

        free((void*)combinedPath);
        free((void*)combinedSubPath);
        if (status) {
            break;
        }

        // write progress after to update the file/folder in progress
        __write_progress(dp->d_name, progress, 0);
    }

    closedir(dfd);
    return status;
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

    checkSize = ZSTD_compress(compressedData, compressedSize, Input, InputLength, ZSTD_defaultCLevel());
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

static int __write_header_metadata(struct VaFs* vafs, const char* name, struct oven_pack_options* options)
{
    struct chef_vafs_feature_package_header*  packageHeader;
    size_t                                    featureSize;
    char*                                     dataPointer;
    int                                       status;

    // count up the data requirements for the package header
    featureSize = sizeof(struct chef_vafs_feature_package_header);
    featureSize += strlen(name);
    featureSize += options->description == NULL ? 0 : strlen(options->description);
    featureSize += options->license == NULL ? 0 : strlen(options->license);
    featureSize += options->author == NULL ? 0 : strlen(options->author);
    featureSize += options->email == NULL ? 0 : strlen(options->email);
    featureSize += options->url == NULL ? 0 : strlen(options->url);
    
    packageHeader = malloc(featureSize);
    if (!packageHeader) {
        fprintf(stderr, "oven: failed to allocate package header\n");
        return -1;
    }

    memcpy(&packageHeader->header.Guid, &g_headerGuid, sizeof(struct VaFsGuid));
    packageHeader->header.Length = featureSize;

    // fill in info
    packageHeader->type = options->type;

    // fill in lengths
    packageHeader->package_length          = strlen(name);
    packageHeader->description_length      = options->description == NULL ? 0 : strlen(options->description);
    packageHeader->license_length          = options->license == NULL ? 0 : strlen(options->license);
    packageHeader->eula_length             = options->eula == NULL ? 0 : strlen(options->eula);
    packageHeader->homepage_length         = options->url == NULL ? 0 : strlen(options->url);
    packageHeader->maintainer_length       = options->author == NULL ? 0 : strlen(options->author);
    packageHeader->maintainer_email_length = options->email == NULL ? 0 : strlen(options->email);

    // fill in data ptrs
    dataPointer = (char*)packageHeader + sizeof(struct chef_vafs_feature_package_header);

    // required
    memcpy(dataPointer, name, packageHeader->package_length);
    dataPointer += packageHeader->package_length;

    if (options->description) {
        memcpy(dataPointer, options->description, packageHeader->description_length);
        dataPointer += packageHeader->description_length;
    }

    if (options->url) {
        memcpy(dataPointer, options->url, packageHeader->homepage_length);
        dataPointer += packageHeader->homepage_length;
    }
    
    if (options->license) {
        memcpy(dataPointer, options->license, packageHeader->license_length);
        dataPointer += packageHeader->license_length;
    }

    if (options->eula) {
        memcpy(dataPointer, options->eula, packageHeader->eula_length);
        dataPointer += packageHeader->eula_length;
    }

    // required
    memcpy(dataPointer, options->author, packageHeader->maintainer_length);
    dataPointer += packageHeader->maintainer_length;

    // required
    memcpy(dataPointer, options->email, packageHeader->maintainer_email_length);
    dataPointer += packageHeader->maintainer_email_length;

    // write the package header
    status = vafs_feature_add(vafs, &packageHeader->header);
    free(packageHeader);
    if (status) {
        fprintf(stderr, "oven: failed to write package header\n");
        return -1;
    }

    // create the package version
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
        fprintf(stderr, "oven: failed to allocate package version\n");
        return -1;
    }

    memcpy(&packageVersion->header.Guid, &g_versionGuid, sizeof(struct VaFsGuid));
    packageVersion->header.Length = featureSize;

    status = __parse_version_string(version, packageVersion);
    if (status) {
        fprintf(stderr, "oven: failed to parse version string %s\n", version);
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
        fprintf(stderr, "oven: failed to open icon file %s\n", path);
        return -1;
    }

    fseek(file, 0, SEEK_END);
    iconSize = ftell(file);
    fseek(file, 0, SEEK_SET);
    iconBuffer = malloc(iconSize);
    if (!iconBuffer) {
        fprintf(stderr, "oven: failed to allocate icon buffer\n");
        fclose(file);
        return -1;
    }

    if (fread(iconBuffer, 1, iconSize, file) != iconSize) {
        fprintf(stderr, "oven: failed to read icon file %s\n", path);
        fclose(file);
        free(iconBuffer);
        return -1;
    }
    fclose(file);

    packageIcon = malloc(sizeof(struct chef_vafs_feature_package_icon) + iconSize);
    if (!packageIcon) {
        fprintf(stderr, "oven: failed to allocate package version\n");
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

static int __write_commands_metadata(struct VaFs* vafs, struct list* commands)
{
    return 0;
}

static int __write_package_metadata(struct VaFs* vafs, const char* name, struct oven_pack_options* options)
{
    int status;

    status = __write_header_metadata(vafs, name, options);
    if (status) {
        fprintf(stderr, "oven: failed to write package header\n");
        return -1;
    }

    status = __write_version_metadata(vafs, options->version);
    if (status) {
        fprintf(stderr, "oven: failed to write package version\n");
        return -1;
    }

    status = __write_icon_metadata(vafs, options->icon);
    if (status) {
        fprintf(stderr, "oven: failed to write package icon\n");
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

int oven_pack(struct oven_pack_options* options)
{
    struct VaFsDirectoryHandle* directoryHandle;
    struct VaFsConfiguration    configuration;
    struct VaFs*                vafs;
    struct progress_context     progressContext = { 0 };
    int                         status;
    char                        tmp[128];
    char*                       start;
    char*                       name;
    int                         i;

    if (!options) {
        errno = EINVAL;
        return -1;
    }

    strbasename(options->name, tmp, sizeof(tmp));
    name = strdup(tmp);
    strcat(tmp, ".pack");

    // get a file count
    __get_count_recursive(
        __get_install_path(),
        NULL,
        options->filters,
        &progressContext.files_total,
        &progressContext.symlinks_total, 
        &progressContext.directories_total
    );

    // we do not want any empty packs
    if (progressContext.files_total == 0) {
        printf("oven: skipping pack %s, no files to pack\n", options->name);
        return 0;
    }

    // initialize settings
    vafs_config_initialize(&configuration);

    // TODO we somehow need to get the arch for the target, do we even care?
    vafs_config_set_architecture(&configuration, __parse_arch(CHEF_ARCHITECTURE_STR));

    status = vafs_create(&tmp[0], &configuration, &vafs);
    if (status) {
        free(name);
        return status;
    }
    
    // install the compression for the pack
    status = __install_filter(vafs);
    if (status) {
        fprintf(stderr, "oven: cannot initialize compression\n");
        goto cleanup;
    }

    status = vafs_directory_open(vafs, "/", &directoryHandle);
    if (status) {
        fprintf(stderr, "oven: cannot open root directory\n");
        goto cleanup;
    }

    status = __write_directory(&progressContext, options->filters, directoryHandle, __get_install_path(), NULL);
    if (status != 0) {
        fprintf(stderr, "oven: unable to write directory\n");
        goto cleanup;
    }
    printf("\n");

    status = __write_package_metadata(vafs, name, options);
    if (status != 0) {
        fprintf(stderr, "oven: unable to write package metadata\n");
    }

cleanup:
    status = vafs_close(vafs);
    free(name);
    return status;
}
