/**
 * Copyright 2024, Philip Meulengracht
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
#include <chef/client.h>
#include <chef/api/package.h>
#include <chef/dirs.h>
#include <chef/diskbuilder.h>
#include <chef/platform.h>
#include <chef/image.h>
#include <chef/ingredient.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <vlog.h>
#include "chef-config.h"

struct __mkcdk_options {
    char*              arch;
    char*              platform;
    unsigned long long size;
};

static void __print_help(void)
{
    printf("Usage: mkcdk [options] image.yaml\n");
    printf("\n");
    printf("Tool to build disk images from either raw files or using chef packages.\n");
    printf("\n");
    printf("Options:\n");
    printf("  -v..\n");
    printf("      Controls the verbosity of mkcdk\n");
    printf("      --version\n");
    printf("      Print the version of mkcdk\n");
    printf("  -h, --help\n");
    printf("      Print this help message\n");
}

struct __package_info {
    char* publisher;
    char* package;
    char* channel;
    char* path;
};

static int __resolve_package_information(const char* str, struct __package_info* info)
{
    char* s = str;
    char* p;

    // seperate out publisher
    p = strchr(s, '/');
    if (p == NULL) {
        return -1;
    }

    info->publisher = platform_strndup(s, (size_t)(p-s));
    
    // set new anchor and skip the separator
    s = p;
    s++;

    p = strchr(s, '/');
    if (p == NULL) {
        info->package = platform_strdup(s);
        info->channel = platform_strdup("stable");
    } else {
        info->package = platform_strndup(s, (size_t)(p-s));
        info->channel = platform_strdup(++p);
    }

    // figure out path
    // TODO:

    return 0;
}

static void __package_info_free(struct __package_info* info)
{
    free(info->publisher);
    info->publisher = NULL;

    free(info->package);
    info->package = NULL;

    free(info->channel);
    info->channel = NULL;
}

static int __resolve_sources(const char* platform, const char* arch, struct chef_image* image)
{
    struct chef_download_params dlParams;
    struct list_item*           i, *j;
    int                         status;

    VLOG_DEBUG("mkcdk", "__resolve_sources()\n");

    status = chefclient_initialize();
    if (status) {
        VLOG_ERROR("mkcdk", "__resolve_sources: failed to initialize chef client\n");
        return status;
    }

    dlParams.arch = arch;
    dlParams.platform = platform;
    dlParams.version = NULL;

    // Download chef packages referred
    list_foreach(&image->partitions, i) {
        struct chef_image_partition* p = (struct chef_image_partition*)i;
        struct __package_info        pi;
        
        // Is the partition special content
        if (p->content != NULL) {
            status = __resolve_package_information(p->content, &pi);
            if (status) {
                VLOG_ERROR("mkcdk", "__resolve_sources: invalid package id: %s\n", p->content);
                VLOG_ERROR("mkcdk", "__resolve_sources: must in format 'publisher/name{/channel}'\n");
                goto cleanup;
            }

            dlParams.publisher = pi.publisher;
            dlParams.package = pi.package;
            dlParams.channel = pi.channel;

            status = chefclient_pack_download(&dlParams, "");
            __package_info_free(&pi);
            if (status) {
                VLOG_ERROR("mkcdk", "__resolve_sources: failed to download %s/%s\n",
                    dlParams.publisher, dlParams.package);
                goto cleanup;
            }
            continue;
        }
        
        // Is there any packages that must be installed
        list_foreach(&p->sources, j) {
            struct chef_image_partition_source* s = (struct chef_image_partition_source*)j;
            
            if (s->type == CHEF_IMAGE_SOURCE_PACKAGE) {
                status = __resolve_package_information(s->source, &pi);
                if (status) {
                    VLOG_ERROR("mkcdk", "__resolve_sources: invalid package id: %s\n", s->source);
                    VLOG_ERROR("mkcdk", "__resolve_sources: must in format 'publisher/name{/channel}'\n");
                    goto cleanup;
                }

                dlParams.publisher = pi.publisher;
                dlParams.package = pi.package;
                dlParams.channel = pi.channel;

                status = chefclient_pack_download(&dlParams, pi.path);
                __package_info_free(&pi);
                if (status) {
                    VLOG_ERROR("mkcdk", "__resolve_sources: failed to download %s/%s\n",
                        dlParams.publisher, dlParams.package);
                    goto cleanup;
                }
                continue;
            }
        }
    }

cleanup:
    chefclient_cleanup();
    return status;
}

static enum chef_diskbuilder_schema __to_diskbuilder_schema(struct chef_image* image)
{
    switch (image->schema) {
        case CHEF_IMAGE_SCHEMA_MBR:
            return CHEF_DISK_SCHEMA_MBR;
        case CHEF_IMAGE_SCHEMA_GPT:
            return CHEF_DISK_SCHEMA_GPT;
        default:
            break;
    }
    VLOG_FATAL("mkcdk", "__to_diskbuilder_schema: unknown schema %i\n", image->schema);
    return 0;
}

static struct ingredient* __open_partition_content(const char* content)
{
    struct __package_info pi;
    struct ingredient*    ig = NULL;
    char*                 path = NULL;
    int                   status;
    VLOG_DEBUG("mkcdk", "__write_image_content(content=%s)\n", content);

    status = __resolve_package_information(content, &pi);
    if (status) {
        // should really not happen, should have been verified earlier
        VLOG_ERROR("mkcdk", "__write_image_content: invalid package id: %s\n", content);
        return NULL;
    }

    // Content means we write the contents of the chef package onto the partition. Usually
    // this will also mean that this contains a bootloader or boot assets.

    // If the chef package is of type BOOTLOADER, then we expect certain structure based
    // on the schema.
    status = ingredient_open(path, &ig);
    if (status) {
        VLOG_ERROR("mkcdk", "__write_image_content: failed to open ingredient %s\n", path);
        goto cleanup;
    }

cleanup:
    __package_info_free(&pi);
    free(path);
    return ig;
}

static int __write_file(struct chef_disk_filesystem* fs, const char* source, const char* dest)
{
    int    status;
    void*  buffer;
    size_t size;
    
    status = platform_readfile(source, &buffer, &size);
    if (status) {
        return status;
    }

    status = fs->create_file(fs, &(struct chef_disk_fs_create_file_params) {
        .path = dest,
        .buffer = buffer,
        .size = size
    });
    free(buffer);
    return status;
}

static int __write_directory(struct chef_disk_filesystem* fs, const char* source, const char* dest)
{
    struct list       files;
    struct list_item* i;
    int               status;

    // ensure target exists
    status = fs->create_directory(fs, &(struct chef_disk_fs_create_directory_params) {
        .path = dest
    });
    if (status) {
        return status;
    }

    status = platform_getfiles(source, 0, &files);
    if (status) {
        return status;
    }

    list_foreach(&files, i) {
        struct platform_file_entry* f = (struct platform_file_entry*)i;
        char*                       np = strpathcombine(dest, f->name);
        switch (f->type) {
            case PLATFORM_FILETYPE_DIRECTORY:
                status = __write_directory(fs, f->path, np);
                break;
            case PLATFORM_FILETYPE_FILE:
                status = __write_file(fs, f->path, np);
                break;
            case PLATFORM_FILETYPE_SYMLINK:
                // ehhhh TODO:
                status = -1;
                break;
            case PLATFORM_FILETYPE_UNKNOWN:
                status = -1;
                break;
        }

        if (status) {
            break;
        }
    }

    platform_getfiles_destroy(&files);
    return status;
}

static int __write_image_content(struct chef_disk_filesystem* fs, struct ingredient* ig)
{
    VLOG_DEBUG("mkcdk", "__write_image_content()\n");

    // ignore contents in resources/*

    return 0;
}

static int __write_image_sources(struct chef_disk_filesystem* fs, struct list* sources)
{
    struct list_item* i;
    int               status = 0;
    VLOG_DEBUG("mkcdk", "__write_image_sources(sourceCount=%i)\n", sources->count);

    list_foreach(sources, i) {
        struct chef_image_partition_source* src = (struct chef_image_partition_source*)i;
        struct __package_info               pinfo;

        switch (src->type) {
            case CHEF_IMAGE_SOURCE_FILE:
                status = __write_file(fs, src->source, src->target);
                break;
            case CHEF_IMAGE_SOURCE_DIRECTORY:
                status = __write_directory(fs, src->source, src->target);
                break;
            case CHEF_IMAGE_SOURCE_PACKAGE:
                status = __resolve_package_information(src->source, &pinfo);
                if (status) {
                    break;
                }
                status = __write_file(fs, pinfo.path, src->target);
                break;
            default:
                status = -1;
                break;
        }

        if (status) {
            break;
        }
    }

    return status;
}

static int __build_image(struct chef_image* image, const char* path, struct __mkcdk_options* options)
{
    struct list_item*        i;
    struct chef_diskbuilder* builder = NULL;
    int                      status;
    VLOG_DEBUG("mkcdk", "__build_image(path=%s)\n", path);

    status = __resolve_sources(options->platform, options->arch, image);
    if (status) {
        VLOG_ERROR("mkcdk", "__build_image: failed to resolve image sources\n");
        goto cleanup;
    }

    builder = chef_diskbuilder_new(&(struct chef_diskbuilder_params) {
        .schema = __to_diskbuilder_schema(image),
        .size = options->size,
        .path = path
    });
    if (builder == NULL) {
        VLOG_ERROR("mkcdk", "__build_image: failed to create diskbuilder\n");
        goto cleanup;
    }

    list_foreach(&image->partitions, i) {
        struct chef_image_partition* pi = (struct chef_image_partition*)i;
        struct chef_disk_partition*  pd;
        struct chef_disk_filesystem* fs = NULL;
        struct ingredient*           ig = NULL;

        pd = chef_diskbuilder_partition_new(builder, &(struct chef_disk_partition_params) {
            .name = pi->label,
            .uuid = pi->guid,
            .size = pi->size,
            .attributes = 0,
        });
        if (pd == NULL) {
            VLOG_ERROR("mkcdk", "__build_image: failed to create partition %s\n", pi->label);
            goto cleanup;
        }

        if (strcmp(pi->fstype, "fat32") == 0) {
            fs = chef_filesystem_fat32_new(pd);
        } else if (strcmp(pi->fstype, "mfs") == 0) {
            fs = chef_filesystem_mfs_new(pd);
        } else {
            VLOG_ERROR("mkcdk", "__build_image: unsupported filesystem: %s\n", pi->fstype);
            status = -1;
            goto cleanup;
        }

        if (pi->content != NULL) {
            ig = __open_partition_content(pi->content);
            if (ig == NULL) {
                VLOG_ERROR("mkcdk", "__build_image: failed to open partition content %s\n", pi->content);
                status = -1;
                goto cleanup;
            }
            fs->set_content(fs, ig);
        }

        status = fs->format(fs);
        if (status) {
            VLOG_ERROR("mkcdk", "__build_image: failed to format partition %s with %s\n", pi->label, pi->fstype);
            ingredient_close(ig);
            goto cleanup;
        }

        if (pi->content != NULL) {
            status = __write_image_content(fs, ig);
        } else {
            status = __write_image_sources(fs, &pi->sources);
        }
        if (status) {
            VLOG_ERROR("mkcdk", "__build_image: failed to write content for %s\n", pi->label);
            ingredient_close(ig);
            goto cleanup;
        }

        status = fs->finish(fs);
        ingredient_close(ig);
        if (status) {
            VLOG_ERROR("mkcdk", "__build_image: failed to finalize filesystem for %s\n", pi->label);
            goto cleanup;
        }

        status = chef_diskbuilder_partition_finish(pd);
        if (status) {
            VLOG_ERROR("mkcdk", "__build_image: failed to finalize partition for %s\n", pi->label);
            goto cleanup;
        }
    }

    status = chef_diskbuilder_finish(builder);
    if (status) {
        VLOG_ERROR("mkcdk", "__build_image: failed to finalize iamge\n");
    }

    // cleanup resources
cleanup:
    chef_diskbuilder_delete(builder);
    return status;
}

static int __read_image_file(char* path, void** bufferOut, size_t* lengthOut)
{
    FILE*  file;
    void*  buffer;
    size_t size, read;

    if (path == NULL) {
        errno = EINVAL;
        return -1;
    }

    file = fopen(path, "r");
    if (!file) {
        fprintf(stderr, "mkcdk: failed to read recipe path: %s\n", path);
        return -1;
    }

    fseek(file, 0, SEEK_END);
    size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    buffer = malloc(size);
    if (!buffer) {
        fprintf(stderr, "mkcdk: failed to allocate memory for recipe: %s\n", strerror(errno));
        fclose(file);
        return -1;
    }

    read = fread(buffer, 1, size, file);
    if (read < size) {
        fprintf(stderr, "mkcdk: failed to read recipe: %s\n", strerror(errno));
        fclose(file);
        return -1;
    }
    
    fclose(file);

    *bufferOut = buffer;
    *lengthOut = size;
    return 0;
}

int main(int argc, char** argv, char** envp)
{
    char*                          imagePath = NULL;
    int                            status;
    int                            logLevel = VLOG_LEVEL_DEBUG;
    struct chef_image*             image = NULL;
    struct __mkcdk_options         options;
    void*                          buffer;
    size_t                         length;
    
    // first argument must be the command if not --help or --version
    if (argc > 1) {
        if (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")) {
            __print_help();
            return 0;
        }

        if (!strcmp(argv[1], "--version")) {
            printf("mkcdk: version " PROJECT_VER "\n");
            return 0;
        }

        for (int i = 2; i < argc; i++) {
            if (!strncmp(argv[i], "-v", 2)) {
                int li = 1;
                while (argv[i][li++] == 'v') {
                    logLevel++;
                }
            } else if (imagePath == NULL) {
                imagePath = argv[i];
            }
        }
    }

    // check against recipe
    if (imagePath == NULL) {
        fprintf(stderr, "mkcdk: image yaml definition must be provided\n");
        return status;
    }

    // initialize the logging system
    vlog_initialize((enum vlog_level)logLevel);

    // initialize the dir library
    status = chef_dirs_initialize(CHEF_DIR_SCOPE_BAKE);
    if (status) {
        VLOG_ERROR("mkcdk", "failed to initialize directories\n");
        goto cleanup;
    }

    status = __read_image_file(imagePath, &buffer, &length);
    if (status) {
        VLOG_ERROR("mkcdk", "failed to load image definition %s\n", imagePath);
        goto cleanup;
    }

    status = chef_image_parse(buffer, length, &image);
    free(buffer);
    if (status) {
        VLOG_ERROR("mkcdk", "failed to parse image definition\n");
        goto cleanup;
    }

    status = __build_image(image, "./pc.img", &options);

cleanup:
    chef_image_destroy(image);
    vlog_cleanup();
    return status;
}
