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

#include <errno.h>
#include <chef/package_image.h>
#include <chef/platform.h>
#include <chef/utils_vafs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vafs/directory.h>
#include <vafs/file.h>
#include <vafs/vafs.h>
#include <vlog.h>
#include <zstd.h>

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

static struct VaFsGuid g_filterGuid = VA_FS_FEATURE_FILTER;
static struct VaFsGuid g_filterOpsGuid = VA_FS_FEATURE_FILTER_OPS;

#define __CHEF_ZSTD_COMPRESSION_LEVEL 15

static ZSTD_CCtx* g_compressContext = NULL;

static const char* __get_filename(const char* path)
{
    const char* filename = (const char*)strrchr(path, CHEF_PATH_SEPARATOR);
    if (filename == NULL) {
        return path;
    }
    return filename + 1;
}

static int __matches_filters(const char* path, const struct list* filters)
{
    struct list_item* item;

    if (filters == NULL || filters->count == 0) {
        return 0;
    }

    list_foreach((struct list*)filters, item) {
        struct list_item_string* filter = (struct list_item_string*)item;
        if (strfilter(filter->value, path, 0) == 0) {
            return 0;
        }
    }
    return -1;
}

static int __get_install_stats(
    struct list*      files,
    const struct list* filters,
    int*              fileCountOut,
    int*              symlinkCountOut)
{
    struct list_item* item;

    if (files == NULL || fileCountOut == NULL || symlinkCountOut == NULL) {
        errno = EINVAL;
        return -1;
    }

    list_foreach(files, item) {
        struct platform_file_entry* entry = (struct platform_file_entry*)item;

        if (__matches_filters(entry->sub_path, filters) != 0) {
            continue;
        }

        switch (entry->type) {
            case PLATFORM_FILETYPE_FILE:
                (*fileCountOut)++;
                break;
            case PLATFORM_FILETYPE_SYMLINK:
                (*symlinkCountOut)++;
                break;
            default:
                break;
        }
    }
    return 0;
}

static void __write_progress(const char* prefix, struct progress_context* context)
{
    int current;
    int total;
    int percent;

    if (context->disabled) {
        return;
    }

    total = context->files_total + context->symlinks_total;
    if (total == 0) {
        return;
    }

    current = context->files + context->symlinks;
    percent = (current * 100) / total;
    if (percent > 100) {
        percent = 100;
    }

    VLOG_TRACE("bake", "%3d%% | %s\n", percent, prefix);
}

static int __write_file(
    struct VaFsDirectoryHandle* directoryHandle,
    const char*                 path,
    const char*                 filename,
    uint32_t                    permissions)
{
    struct VaFsFileHandle* fileHandle = NULL;
    FILE*                  file = NULL;
    long                   fileSize;
    void*                  fileBuffer = NULL;
    size_t                 bytesRead;
    int                    status;

    status = vafs_directory_create_file(directoryHandle, filename, permissions, &fileHandle);
    if (status != 0) {
        return status;
    }

    file = fopen(path, "rb");
    if (file == NULL) {
        VLOG_ERROR("bake", "unable to open file %s\n", path);
        vafs_file_close(fileHandle);
        return -1;
    }

    fseek(file, 0, SEEK_END);
    fileSize = ftell(file);
    rewind(file);
    if (fileSize > 0) {
        size_t writeFailed;

        fileBuffer = malloc((size_t)fileSize);
        if (fileBuffer == NULL) {
            fclose(file);
            vafs_file_close(fileHandle);
            errno = ENOMEM;
            return -1;
        }

        bytesRead = fread(fileBuffer, 1, (size_t)fileSize, file);
        if (bytesRead != (size_t)fileSize) {
            VLOG_ERROR("bake", "only partial read %s\n", path);
        }

        writeFailed = vafs_file_write(fileHandle, fileBuffer, (size_t)fileSize);
        if (writeFailed) {
            status = -1;
        }
    }

    free(fileBuffer);
    fclose(file);

    if (status != 0) {
        VLOG_ERROR("bake", "failed to write file '%s': %s\n", filename, strerror(errno));
        vafs_file_close(fileHandle);
        return -1;
    }

    status = vafs_file_close(fileHandle);
    if (status != 0) {
        VLOG_ERROR("bake", "failed to close file '%s'\n", filename);
        return -1;
    }
    return 0;
}

static int __write_directory(
    struct progress_context*    progress,
    const struct list*          filters,
    struct VaFsDirectoryHandle* directoryHandle,
    const char*                 path,
    const char*                 subPath)
{
    struct dirent* dp;
    DIR*           dfd;
    int            status = 0;

    dfd = opendir(path);
    if (dfd == NULL) {
        VLOG_ERROR("bake", "can't open initrd folder\n");
        return -1;
    }

    while ((dp = readdir(dfd)) != NULL) {
        struct platform_stat stats;
        const char*          combinedPath;
        const char*          combinedSubPath;

        if (strcmp(dp->d_name, ".") == 0 || strcmp(dp->d_name, "..") == 0) {
            continue;
        }

        combinedPath = strpathcombine(path, dp->d_name);
        combinedSubPath = strpathcombine(subPath, dp->d_name);
        if (combinedPath == NULL || combinedSubPath == NULL) {
            free((void*)combinedPath);
            free((void*)combinedSubPath);
            status = -1;
            break;
        }

        if (__matches_filters(combinedSubPath, filters) != 0) {
            free((void*)combinedPath);
            free((void*)combinedSubPath);
            continue;
        }

        status = platform_stat(combinedPath, &stats);
        if (status != 0) {
            VLOG_ERROR("bake", "failed to get filetype for '%s'\n", combinedPath);
            free((void*)combinedPath);
            free((void*)combinedSubPath);
            continue;
        }

        __write_progress(dp->d_name, progress);

        if (stats.type == PLATFORM_FILETYPE_DIRECTORY) {
            struct VaFsDirectoryHandle* subdirectoryHandle;

            status = vafs_directory_create_directory(directoryHandle, dp->d_name, stats.permissions, &subdirectoryHandle);
            if (status != 0) {
                VLOG_ERROR("bake", "failed to create directory '%s'\n", dp->d_name);
            } else {
                status = __write_directory(progress, filters, subdirectoryHandle, combinedPath, combinedSubPath);
                if (status != 0) {
                    VLOG_ERROR("bake", "unable to write directory %s\n", combinedPath);
                }
                if (vafs_directory_close(subdirectoryHandle) != 0) {
                    VLOG_ERROR("bake", "failed to close directory '%s'\n", combinedPath);
                    status = -1;
                }
            }
        } else if (stats.type == PLATFORM_FILETYPE_FILE) {
            status = __write_file(directoryHandle, combinedPath, dp->d_name, stats.permissions);
            if (status != 0) {
                VLOG_ERROR("bake", "unable to write file %s\n", dp->d_name);
            }
            progress->files++;
        } else if (stats.type == PLATFORM_FILETYPE_SYMLINK) {
            char* linkpath;

            status = platform_readlink(combinedPath, &linkpath);
            if (status != 0) {
                VLOG_ERROR("bake", "failed to read link %s\n", combinedPath);
            } else {
                status = vafs_directory_create_symlink(directoryHandle, dp->d_name, linkpath);
                free(linkpath);
                if (status != 0) {
                    VLOG_ERROR("bake", "failed to create symlink %s\n", combinedPath);
                }
            }
            progress->symlinks++;
        } else {
            VLOG_ERROR("bake", "unknown filetype for '%s'\n", combinedPath);
            status = 0;
        }

        free((void*)combinedPath);
        free((void*)combinedSubPath);
        if (status != 0) {
            break;
        }

        __write_progress(dp->d_name, progress);
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
    if (compressedData == NULL) {
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
        free(compressedData);
        return -1;
    }

    *Output = compressedData;
    *OutputLength = (uint32_t)checkSize;
    return 0;
}

static int __zstd_decode(void* Input, uint32_t InputLength, void* Output, uint32_t* OutputLength)
{
    size_t decompressedSize;

    decompressedSize = ZSTD_decompress(Output, *OutputLength, Input, InputLength);
    if (ZSTD_isError(decompressedSize)) {
        return -1;
    }
    *OutputLength = (uint32_t)decompressedSize;
    return 0;
}

static int __set_filter_ops(struct VaFs* vafs)
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
    if (status != 0) {
        return status;
    }
    return __set_filter_ops(vafs);
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

int chef_package_image_create(const struct chef_package_image_options* options)
{
    struct VaFsDirectoryHandle* directoryHandle = NULL;
    struct VaFsConfiguration    configuration;
    struct VaFs*                vafs = NULL;
    struct list                 files = { 0 };
    struct progress_context     progressContext = { 0 };
    const char*                 progressName;
    int                         status;

    if (options == NULL || options->input_dir == NULL || options->output_path == NULL || options->manifest == NULL) {
        errno = EINVAL;
        return -1;
    }

    progressName = options->manifest->name != NULL ? options->manifest->name : __get_filename(options->output_path);
    VLOG_DEBUG("bake", "chef_package_image_create(name=%s, path=%s)\n", progressName, options->output_path);

    status = platform_getfiles(options->input_dir, 1, &files);
    if (status != 0) {
        VLOG_ERROR("bake", "failed to get files marked for install\n");
        return -1;
    }

    __get_install_stats(&files, options->filters, &progressContext.files_total, &progressContext.symlinks_total);
    if (progressContext.files_total == 0) {
        VLOG_TRACE("bake", "skipping pack %s, no files to pack\n", progressName);
        status = 0;
        goto cleanup;
    }

    vafs_config_initialize(&configuration);
    vafs_config_set_architecture(&configuration, __parse_arch(options->manifest->architecture));
    vafs_config_set_block_size(&configuration, 1024 * 1024);

    status = vafs_create(options->output_path, &configuration, &vafs);
    if (status != 0) {
        goto cleanup;
    }

    g_compressContext = ZSTD_createCCtx();
    status = __install_filter(vafs);
    if (status != 0) {
        VLOG_ERROR("bake", "cannot initialize compression\n");
        goto cleanup;
    }

    status = vafs_directory_open(vafs, "/", &directoryHandle);
    if (status != 0) {
        VLOG_ERROR("bake", "cannot open root directory\n");
        goto cleanup;
    }

    status = __write_directory(&progressContext, options->filters, directoryHandle, options->input_dir, NULL);
    if (status != 0) {
        VLOG_ERROR("bake", "unable to write directory\n");
        goto cleanup;
    }

    __finalize_progress(&progressContext, progressName);

    status = chef_package_manifest_write(vafs, options->manifest);
    if (status != 0) {
        VLOG_ERROR("bake", "unable to write package metadata\n");
    }

cleanup:
    if (directoryHandle != NULL) {
        vafs_directory_close(directoryHandle);
    }
    if (vafs != NULL) {
        vafs_close(vafs);
    }
    platform_getfiles_destroy(&files);
    if (g_compressContext != NULL) {
        ZSTD_freeCCtx(g_compressContext);
        g_compressContext = NULL;
    }
    return status;
}