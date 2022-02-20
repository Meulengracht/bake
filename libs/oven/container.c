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

struct VaFsFeatureFilter {
    struct VaFsFeatureHeader Header;
};

static struct VaFsGuid g_filterGuid    = VA_FS_FEATURE_FILTER;
static struct VaFsGuid g_filterOpsGuid = VA_FS_FEATURE_FILTER_OPS;


static const char* __get_relative_path(
	const char* root,
	const char* path)
{
	const char* relative = path;
	if (strncmp(path, root, strlen(root)) == 0)
		relative = path + strlen(root);
	return relative;
}

static const char* __get_filename(
	const char* path)
{
	const char* filename = (const char*)strrchr(path, '/');
	if (filename == NULL)
		filename = path;
	else
		filename++;
	return filename;
}

static int __write_file(
	struct VaFsDirectoryHandle* directoryHandle,
	const char*                 path,
	const char*                 filename)
{
	struct VaFsFileHandle* fileHandle;
	FILE*                  file;
	long                   fileSize;
	void*                  fileBuffer;
	int                    status;

	// create the VaFS file
	status = vafs_directory_open_file(directoryHandle, filename, &fileHandle);
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
	fileBuffer = malloc(fileSize);
	rewind(file);
	fread(fileBuffer, 1, fileSize, file);
	fclose(file);

	// write the file to the VaFS file
	status = vafs_file_write(fileHandle, fileBuffer, fileSize);
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
	struct VaFsDirectoryHandle* directoryHandle,
	const char*                 path)
{
    struct dirent* dp;
	DIR*           dfd;
	int            status = 0;
	char*          filepathBuffer;
	printf("oven: writing directory '%s'\n", path);

	if ((dfd = opendir(path)) == NULL) {
		fprintf(stderr, "oven: can't open initrd folder\n");
		return -1;
	}

    filepathBuffer = malloc(512);
	while ((dp = readdir(dfd)) != NULL) {
		if (strcmp(dp->d_name, ".") == 0 || strcmp(dp->d_name, "..") == 0)
			continue;

		// only append a '/' if not provided
		if (path[strlen(path) - 1] != '/')
			sprintf(filepathBuffer, "%s/%s", path, dp->d_name);
		else
			sprintf(filepathBuffer, "%s%s", path, dp->d_name);
		printf("oven: found '%s'\n", filepathBuffer);

		if (!platform_isdir(filepathBuffer)) {
			struct VaFsDirectoryHandle* subdirectoryHandle;
			status = vafs_directory_open_directory(directoryHandle, dp->d_name, &subdirectoryHandle);
			if (status) {
				fprintf(stderr, "oven: failed to create directory '%s'\n", dp->d_name);
				continue;
			}

			status = __write_directory(subdirectoryHandle, filepathBuffer);
			if (status != 0) {
				fprintf(stderr, "oven: unable to write directory %s\n", filepathBuffer);
				break;
			}

			status = vafs_directory_close(subdirectoryHandle);
			if (status) {
				fprintf(stderr, "oven: failed to close directory '%s'\n", filepathBuffer);
				break;
			}
		}
		else {
			status = __write_file(directoryHandle, filepathBuffer, dp->d_name);
			if (status != 0) {
				fprintf(stderr, "oven: unable to write file %s\n", dp->d_name);
				break;
			}
		}
	}

	free(filepathBuffer);
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

int oven_pack(struct oven_pack_options* options)
{
    struct VaFsDirectoryHandle* directoryHandle;
    struct VaFs*                vafs;
    int                         status;
    char                        tmp[128];
    int                         i;

    if (!options) {
        errno = EINVAL;
        return -1;
    }

    memset(&tmp[0], 0, sizeof(tmp));
    for (i = 0; options->name[i] && options->name[i] != '.'; i++) {
        tmp[i] = options->name[i];
    }
    strcat(tmp, ".container");

    // TODO arch
    status = vafs_create(&tmp[0], VaFsArchitecture_X64, &vafs);
    if (status) {
        return status;
    }
    
    // install the compression for the container
    status = __install_filter(vafs);
    if (status) {
        fprintf(stderr, "oven: cannot initialize compression\n");
        return -1;
    }

	status = vafs_directory_open(vafs, "/", &directoryHandle);
	if (status) {
		fprintf(stderr, "oven: cannot open root directory\n");
		return -1;
	}

    status = __write_directory(directoryHandle, ".oven/install");
    if (status != 0) {
        fprintf(stderr, "oven: unable to write directory\n");
    }

    status = vafs_close(vafs);
    return status;
}
