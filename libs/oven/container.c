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

int __get_count_recursive(const char *path, int* fileCountOut, int* SymlinkCountOut, int* dirCountOut)
{
    struct dirent* direntp;
    DIR*           dir_ptr = NULL;

    if (!path) {
        errno = EINVAL;
        return -1;
    }

    if ((dir_ptr = opendir(path)) == NULL) {
        return -1;
    }

    while ((direntp = readdir(dir_ptr))) {
        if (strcmp(direntp->d_name,".") == 0 || strcmp(direntp->d_name,"..") == 0) {
             continue;
        }

        switch (direntp->d_type) {
            case DT_REG:
                (*fileCountOut)++;
                break;
            case DT_DIR: {
                char* npath;

                (*dirCountOut)++;
                
                npath = malloc(strlen(path)+strlen(direntp->d_name)+2);
                if (npath == NULL) {
                    errno = ENOMEM;
                    return -1;
                }
                
                sprintf(npath, "%s/%s", path, direntp->d_name);
                
                if (__get_count_recursive(npath, fileCountOut, SymlinkCountOut, dirCountOut) == -1) {
                    free(npath);
                    return -1;
                }

                free(npath);
            } break;
            case DT_LNK:
                (*SymlinkCountOut)++;
                break;
            default:
                break;
        }
    }
    closedir(dir_ptr);
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
	if (context->files_total) {
		printf(" %i/%i files", context->files, context->files_total);
	}
	if (context->directories_total) {
		printf(" %i/%i directories", context->directories, context->directories_total);
	}
	if (context->symlinks_total) {
		printf(" %i/%i symlinks", context->symlinks, context->symlinks_total);
	}
    fflush(stdout);
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
	size_t                 bytesRead;
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
	bytesRead = fread(fileBuffer, 1, fileSize, file);
	if (bytesRead != fileSize) {
		fprintf(stderr, "oven: only partial read %s\n", path);
	}
	fclose(file);

	// write the file to the VaFS file
	status = vafs_file_write(fileHandle, fileBuffer, fileSize);
	free(fileBuffer);
	
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
		enum platform_filetype fileType;

		if (strcmp(dp->d_name, ".") == 0 || strcmp(dp->d_name, "..") == 0) {
			continue;
		}

		// only append a '/' if not provided
		if (path[strlen(path) - 1] != '/')
			sprintf(filepathBuffer, "%s/%s", path, dp->d_name);
		else
			sprintf(filepathBuffer, "%s%s", path, dp->d_name);

		status = platform_filetype(filepathBuffer, &fileType);
		if (status != 0) {
			fprintf(stderr, "oven: failed to get filetype for '%s'\n", filepathBuffer);
			continue;
		}

		// write progress before to update the file/folder in progress
        __write_progress(filepathBuffer, progress);
		if (fileType == PLATFORM_FILETYPE_DIRECTORY) {
			struct VaFsDirectoryHandle* subdirectoryHandle;
			status = vafs_directory_open_directory(directoryHandle, dp->d_name, &subdirectoryHandle);
			if (status) {
				fprintf(stderr, "oven: failed to create directory '%s'\n", dp->d_name);
				continue;
			}

			status = __write_directory(progress, subdirectoryHandle, filepathBuffer);
			if (status != 0) {
				fprintf(stderr, "oven: unable to write directory %s\n", filepathBuffer);
				break;
			}

			status = vafs_directory_close(subdirectoryHandle);
			if (status) {
				fprintf(stderr, "oven: failed to close directory '%s'\n", filepathBuffer);
				break;
			}
			progress->directories++;
		} else if (fileType == PLATFORM_FILETYPE_FILE) {
			status = __write_file(directoryHandle, filepathBuffer, dp->d_name);
			if (status != 0) {
				fprintf(stderr, "oven: unable to write file %s\n", dp->d_name);
				break;
			}
			progress->files++;
		} else if (fileType == PLATFORM_FILETYPE_SYMLINK) {
			char* linkpath;
			status = platform_readlink(filepathBuffer, &linkpath);
			if (status != 0) {
				fprintf(stderr, "oven: failed to read link %s\n", filepathBuffer);
				break;
			}

			status = vafs_directory_create_symlink(directoryHandle, dp->d_name, linkpath);
			free(linkpath);

			if (status != 0) {
				fprintf(stderr, "oven: failed to create symlink %s\n", filepathBuffer);
				break;
			}
			progress->symlinks++;
		} else {
			fprintf(stderr, "oven: unknown filetype for '%s'\n", filepathBuffer);
		}

		// write progress after to update the file/folder in progress
        __write_progress(filepathBuffer, progress);
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
    
    pointer    = pointerEnd + 1;
	pointerEnd = NULL; // consume rest
	
	// extract second part
    version->minor    = strtol(pointer, &pointerEnd, 10);
    version->revision = 0;
    return 0;
}

static int __write_package_metadata(struct VaFs* vafs, const char* name, struct oven_pack_options* options)
{
	struct chef_vafs_feature_package_header*  packageHeader;
	struct chef_vafs_feature_package_version* packageVersion;
	size_t                                    featureSize;
	char*                                     dataPointer;
	char*                                     tagPointer;
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
	packageHeader->type  = options->type;

	// fill in lengths
	packageHeader->package_length          = strlen(name);
	packageHeader->description_length      = options->description == NULL ? 0 : strlen(options->description);
	packageHeader->license_length          = options->license == NULL ? 0 : strlen(options->license);
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

	if (options->license) {
		memcpy(dataPointer, options->license, packageHeader->license_length);
		dataPointer += packageHeader->license_length;
	}

	if (options->url) {
		memcpy(dataPointer, options->url, packageHeader->homepage_length);
		dataPointer += packageHeader->homepage_length;
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
	featureSize = sizeof(struct chef_vafs_feature_package_version);
	tagPointer = strchr(options->version, '+');
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

	status = __parse_version_string(options->version, packageVersion);
	if (status) {
		fprintf(stderr, "oven: failed to parse version string %s\n", options->version);
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
	return status;
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

    memset(&tmp[0], 0, sizeof(tmp));
	start = strrchr(options->name, '/');
	if (start == NULL) {
		start = (char*)options->name;
	} else {
		start++;
	}

    for (i = 0; start[i] && start[i] != '.'; i++) {
        tmp[i] = start[i];
    }
	name = strdup(tmp);
    strcat(tmp, ".pack");

	// get a file count
	__get_count_recursive(__get_install_path(),
		&progressContext.files_total,
		&progressContext.symlinks_total, 
		&progressContext.directories_total
	);

	// initialize settings
	vafs_config_initialize(&configuration);
    
	// TODO arch
	vafs_config_set_architecture(&configuration, VaFsArchitecture_X64);

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

    status = __write_directory(&progressContext, directoryHandle, __get_install_path());
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
