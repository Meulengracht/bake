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
#include <time.h>
#include <vlog.h>
#include "chef-config.h"

struct __mkcdk_options {
    char*    out;
    char*    arch;
    char*    platform;
    uint64_t size;
    uint64_t sector_size;
};

static void __print_help(void)
{
    printf("Usage: mkcdk [options] image.yaml\n");
    printf("\n");
    printf("Tool to build disk images from either raw files or using chef packages.\n");
    printf("\n");
    printf("Options:\n");
    printf("  -o, --output\n");
    printf("      Path of the resulting image file, default is 'pc.img'\n");
    printf("  -p, --platform\n");
    printf("      Target platform of the image, this will affect how packages are resolved\n");
    printf("  -a, --archs\n");
    printf("      Target architecture of the image, this will affect how packages are resolved\n");
    printf("  -s, --size\n");
    printf("      The size of the generated image, default is 4GB\n");
    printf("  -z, --sector-size\n");
    printf("      The sector size to use for the generated image, default is 512 bytes\n");
    printf("  -v..\n");
    printf("      Controls the verbosity of mkcdk\n");
    printf("      --version\n");
    printf("      Print the version of mkcdk\n");
    printf("  -h, --help\n");
    printf("      Print this help message\n");
}

struct __image_context {
    const char* work_directory;
    const char* arch;
    const char* platform;
};

struct __package_info {
    char* publisher;
    char* package;
    char* channel;
    char* path;
};

static int __resolve_package_information(const char* str, struct __image_context* context, struct __package_info* info)
{
    char        path[PATH_MAX];
    const char* s = str;
    char*       p;

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

    snprintf(
        &path[0],
        sizeof(path) - 1,
        "%s" CHEF_PATH_SEPARATOR_S "%s-%s-%s",
        context->work_directory, 
        info->publisher,
        info->package,
        info->channel
    );
    info->path = platform_strdup(&path[0]);
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
    free(info->path);
    info->path = NULL;
}

static int __resolve_sources(struct __image_context* context, struct chef_image* image)
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

    dlParams.arch = context->arch;
    dlParams.platform = context->platform;
    dlParams.version = NULL;

    // Download chef packages referred
    list_foreach(&image->partitions, i) {
        struct chef_image_partition* p = (struct chef_image_partition*)i;
        struct __package_info        pi;
        
        // Is the partition special content
        if (p->content != NULL) {
            status = __resolve_package_information(p->content, context, &pi);
            if (status) {
                VLOG_ERROR("mkcdk", "__resolve_sources: invalid package id: %s\n", p->content);
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
        
        // Is there any packages that must be installed
        list_foreach(&p->sources, j) {
            struct chef_image_partition_source* s = (struct chef_image_partition_source*)j;
            
            if (s->type == CHEF_IMAGE_SOURCE_PACKAGE) {
                status = __resolve_package_information(s->source, context, &pi);
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

static struct ingredient* __open_partition_content(struct __image_context* context, const char* content)
{
    struct __package_info pi;
    struct ingredient*    ig = NULL;
    char*                 path = NULL;
    int                   status;
    VLOG_DEBUG("mkcdk", "__write_image_content(content=%s)\n", content);

    status = __resolve_package_information(content, context, &pi);
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

static int __ensure_directory(struct chef_disk_filesystem* fs, const char* path)
{
    char   ccpath[512];
    char*  p = NULL;
    size_t length;
    int    status;
    VLOG_DEBUG("mkcdk", "__ensure_directory(path=%s)\n", path);

    status = snprintf(ccpath, sizeof(ccpath), "%s", path);
    if (status >= sizeof(ccpath)) {
        errno = ENAMETOOLONG;
        return -1; 
    }

    length = strlen(ccpath);
    if (ccpath[length - 1] == '/') {
        ccpath[length - 1] = 0;
    }

    for (p = ccpath + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;

            status = fs->create_directory(fs, &(struct chef_disk_fs_create_directory_params) {
                .path = ccpath
            });
            if (status) {
                return status;
            }

            *p = '/';
        }
    }
    return fs->create_directory(fs, &(struct chef_disk_fs_create_directory_params) {
        .path = ccpath
    });
}

static int __ensure_file_directory(struct chef_disk_filesystem* fs, const char* dest)
{
    char   tmp[512];
    char*  p;
    VLOG_DEBUG("mkcdk", "__ensure_file_directory(dest=%s)\n", dest);

    strcpy(&tmp[0], dest);
    p = strrchr(&tmp[0], '/');
    if (p == NULL) {
        VLOG_DEBUG("mkcdk", "__ensure_file_directory: no directory for %s\n", dest);
        return 0;
    }
    
    *p = 0;
    return __ensure_directory(fs, &tmp[0]);
}

static int __write_file(struct chef_disk_filesystem* fs, const char* source, const char* dest)
{
    int    status;
    void*  buffer;
    size_t size;
    VLOG_DEBUG("mkcdk", "__write_file(source=%s, dest=%s)\n", source, dest);

    status = __ensure_file_directory(fs, dest);
    if (status) {
        VLOG_ERROR("mkcdk", "__write_file: failed to create directory for file %s\n", dest);
        return status;
    }

    status = platform_readfile(source, &buffer, &size);
    if (status) {
        VLOG_ERROR("mkcdk", "__write_file: failed to read file %s\n", source);
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
    VLOG_DEBUG("mkcdk", "__write_directory(source=%s, dest=%s)\n", source, dest);

    status = __ensure_directory(fs, dest);
    if (status) {
        VLOG_ERROR("mkcdk", "__write_directory: failed to create directory for %s\n", dest);
        return status;
    }

    status = platform_getfiles(source, 0, &files);
    if (status) {
        VLOG_ERROR("mkcdk", "__write_directory: failed to read directory %s\n", source);
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
                VLOG_ERROR("mkcdk", "__write_directory: symlinks missing handling\n");
                status = -1;
                break;
            case PLATFORM_FILETYPE_UNKNOWN:
                VLOG_ERROR("mkcdk", "__write_directory: unknown file type\n");
                status = -1;
                break;
        }
        free(np);

        if (status) {
            VLOG_ERROR("mkcdk", "__write_directory: failed to install file %s\n", f->path);
            break;
        }
    }

    platform_getfiles_destroy(&files);
    return status;
}

static int __write_image_content(struct chef_disk_filesystem* fs, const char* content, struct ingredient* ig)
{
    struct list       files;
    struct list_item* i;
    int               status;
    VLOG_DEBUG("mkcdk", "__write_image_content()\n");

    status = platform_getfiles(content, 0, &files);
    if (status) {
        VLOG_ERROR("mkcdk", "__write_image_content: failed to read directory %s\n", content);
        return status;
    }

    list_foreach(&files, i) {
        struct platform_file_entry* f = (struct platform_file_entry*)i;
        switch (f->type) {
            case PLATFORM_FILETYPE_DIRECTORY:
                // ignore contents in resources/*
                if (strcmp(f->name, "resources") != 0) {
                    status = __write_directory(fs, f->path, f->name);
                }
                break;
            case PLATFORM_FILETYPE_FILE:
                status = __write_file(fs, f->path, f->name);
                break;
            case PLATFORM_FILETYPE_SYMLINK:
                VLOG_ERROR("mkcdk", "__write_image_content: symlinks missing handling\n");
                status = -1;
                break;
            case PLATFORM_FILETYPE_UNKNOWN:
                VLOG_ERROR("mkcdk", "__write_image_content: unknown file type\n");
                status = -1;
                break;
        }

        if (status) {
            VLOG_ERROR("mkcdk", "__write_image_content: failed to install file %s\n", f->path);
            break;
        }
    }

    platform_getfiles_destroy(&files);
    return 0;
}

static int __write_raw(struct chef_disk_filesystem* fs, const char* source, const char* target)
{
    struct chef_disk_fs_write_raw_params params = { 0 };
    int                                  status;
    char**                               options;

    options = strsplit(target, ',');
    if (options == NULL) {
        VLOG_ERROR("mkcdk", "__write_raw: failed to parse %s\n", target);
        return -1;
    }

    for (int i = 0; options[i] != NULL; i++) {
        if (strncmp(options[i], "sector=", 7) == 0) {
            char* p = options[i];
            params.sector = strtoull(p + 7, NULL, 10);
        } else if (strncmp(options[i], "offset=", 7) == 0) {
            char* p = options[i];
            params.offset = strtoull(p + 7, NULL, 10);
        } else {
            VLOG_ERROR("mkcdk", "__write_raw: unrecognized option: %s\n", options[i]);
            strsplit_free(options);
            return -1;
        }
    }

    status = platform_readfile(source, (void**)&params.buffer, &params.size);
    if (status) {
        VLOG_ERROR("mkcdk", "__write_raw: failed to read %s\n", source);
        strsplit_free(options);
        return status;
    }

    status = fs->write_raw(fs, &params);
    if (status) {
        VLOG_ERROR("mkcdk", "__write_raw: failed to write raw image\n");
    }
    free((void*)params.buffer);
    strsplit_free(options);
    return status;
}

static int __write_image_sources(struct chef_disk_filesystem* fs, struct __image_context* context, struct list* sources)
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
                status = __resolve_package_information(src->source, context, &pinfo);
                if (status) {
                    VLOG_ERROR("mkcdk", "__write_image_sources: failed to resolve source %s\n", src->source);
                    break;
                }
                status = __write_file(fs, pinfo.path, src->target);
                break;
            case CHEF_IMAGE_SOURCE_RAW:
                status = __write_raw(fs, src->source, src->target);
                break;
            default:
                VLOG_ERROR("mkcdk", "__write_image_sources: unsupported source type\n");
                status = -1;
                break;
        }

        if (status) {
            VLOG_ERROR("mkcdk", "__write_image_sources: failed to install source %s\n", src->target);
            break;
        }
    }

    return status;
}

static enum chef_partition_attributes __to_partition_attributes(struct list* attribs)
{
    enum chef_partition_attributes flags = 0;
    struct list_item*              i;

    list_foreach(attribs, i) {
        struct list_item_string* str = (struct list_item_string*)i;
        if (strcmp(str->value, "boot") == 0) {
            flags |= CHEF_PARTITION_ATTRIBUTE_BOOT;
        } else if (strcmp(str->value, "readonly") == 0) {
            flags |= CHEF_PARTITION_ATTRIBUTE_READONLY;
        } else if (strcmp(str->value, "noautomount") == 0) {
            flags |= CHEF_PARTITION_ATTRIBUTE_NOAUTOMOUNT;
        }
    }
    return flags;
}

static int __build_image(struct chef_image* image, const char* path, struct __mkcdk_options* options)
{
    struct list_item*        i;
    struct chef_diskbuilder* builder = NULL;
    struct __image_context   context = { NULL };
    int                      status;
    char                     tmpBuffer[256];
    VLOG_DEBUG("mkcdk", "__build_image(path=%s)\n", path);

    context.arch = options->arch;
    context.platform = options->platform;
    context.work_directory = platform_tmpdir();

    status = __resolve_sources(&context, image);
    if (status) {
        VLOG_ERROR("mkcdk", "__build_image: failed to resolve image sources\n");
        goto cleanup;
    }

    builder = chef_diskbuilder_new(&(struct chef_diskbuilder_params) {
        .schema = __to_diskbuilder_schema(image),
        .size = options->size,
        .sector_size = (unsigned int)options->sector_size,
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
        const char*                  contentPath = NULL;

        pd = chef_diskbuilder_partition_new(builder, &(struct chef_disk_partition_params) {
            .name = pi->label,
            .guid = pi->guid,
            .type = pi->type,
            .size = pi->size,
            .attributes = __to_partition_attributes(&pi->attributes),
            .work_directory = context.work_directory
        });
        if (pd == NULL) {
            VLOG_ERROR("mkcdk", "__build_image: failed to create partition %s\n", pi->label);
            goto cleanup;
        }

        if (strcmp(pi->fstype, "fat32") == 0) {
            fs = chef_filesystem_fat32_new(pd, &(struct chef_disk_filesystem_params) {
                .sector_size = options->sector_size
            });
        } else if (strcmp(pi->fstype, "mfs") == 0) {
            fs = chef_filesystem_mfs_new(pd, &(struct chef_disk_filesystem_params) {
                .sector_size = options->sector_size
            });
        } else {
            VLOG_ERROR("mkcdk", "__build_image: unsupported filesystem: %s\n", pi->fstype);
            status = -1;
            goto cleanup;
        }

        if (pi->content != NULL) {
            ig = __open_partition_content(&context, pi->content);
            if (ig == NULL) {
                VLOG_ERROR("mkcdk", "__build_image: failed to open partition content %s\n", pi->content);
                status = -1;
                goto cleanup;
            }
            
            snprintf(&tmpBuffer[0], sizeof(tmpBuffer) - 1, "%s-content", pi->label);
            contentPath = strpathcombine(context.work_directory, &tmpBuffer[0]);
            if (contentPath == NULL) {
                VLOG_ERROR("mkcdk", "__build_image: failed to allocate memory\n");
                status = -1;
                goto cleanup;
            }
            
            // unpack to intermediate directory
            status = ingredient_unpack(ig, contentPath, NULL, NULL);
            if (status) {
                VLOG_ERROR("mkcdk", "__build_image: failed to unpack content to %s\n", contentPath);
                goto cleanup;
            }

            fs->set_content(fs, contentPath);
        }

        status = fs->format(fs);
        if (status) {
            VLOG_ERROR("mkcdk", "__build_image: failed to format partition %s with %s\n", pi->label, pi->fstype);
            ingredient_close(ig);
            goto cleanup;
        }

        if (pi->content != NULL) {
            status = __write_image_content(fs, contentPath, ig);
            ingredient_close(ig);
        } else {
            status = __write_image_sources(fs, &context, &pi->sources);
        }
        if (status) {
            VLOG_ERROR("mkcdk", "__build_image: failed to write content for %s\n", pi->label);
            goto cleanup;
        }

        status = fs->finish(fs);
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
        VLOG_ERROR("mkcdk", "__build_image: failed to finalize image\n");
    }

    // cleanup resources
cleanup:
    chef_diskbuilder_delete(builder);
    free((void*)context.work_directory);
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

static uint64_t __parse_quantity(const char* size)
{
    char*    end = NULL;
    uint64_t number = strtoull(size, &end, 10);
    switch (*end)  {
        case 'G':
            number *= 1024;
            // fallthrough
        case 'M':
            number *= 1024;
            // fallthrough
        case 'K':
            number *= 1024;
            break;
        default:
            break;
    }
    return number;
}

static char* __split_switch(char** argv, int argc, int* i)
{
    char* split = strchr(argv[*i], '=');
    if (split != NULL) {
        return split + 1;
    }
    if ((*i + 1) < argc) {
        return argv[++(*i)];
    }
    return NULL;
}

static int __parse_string_switch(char** argv, int argc, int* i, const char* s, size_t sl, const char* l, size_t ll, const char* defaultValue, char** out)
{
    if (strncmp(argv[*i], s, sl) == 0 || strncmp(argv[*i], l, ll) == 0) {
        char* value = __split_switch(argv, argc, i);
        *out = value != NULL ? value : (char*)defaultValue;
        return 0;
    }
    return -1;
}

static int __parse_quantity_switch(char** argv, int argc, int* i, const char* s, size_t sl, const char* l, size_t ll, uint64_t defaultValue, uint64_t* out)
{
    if (strncmp(argv[*i], s, sl) == 0 || strncmp(argv[*i], l, ll) == 0) {
        char* value = __split_switch(argv, argc, i);
        *out = value != NULL ? __parse_quantity(value) : defaultValue;
        return 0;
    }
    return -1;
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

    options.out = "./pc.img";
    options.arch = CHEF_ARCHITECTURE_STR;
    options.platform = CHEF_PLATFORM_STR;
    options.sector_size = 512;
    options.size = (1024ULL * 1024ULL) * 4096ULL; // 4GB

    // needed for guids
    srand(clock());

    // needed for guids
    srand(clock());

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

        for (int i = 1; i < argc; i++) {
            if (!strncmp(argv[i], "-v", 2)) {
                int li = 1;
                while (argv[i][li++] == 'v') {
                    logLevel++;
                }
            } else if (!__parse_string_switch(argv, argc, &i, "-o", 2, "--output", 8, NULL, (char**)&options.out)) {
                continue;
            } else if (!__parse_string_switch(argv, argc, &i, "-p", 2, "--platform", 10, NULL, (char**)&options.platform)) {
                continue;
            } else if (!__parse_string_switch(argv, argc, &i, "-a", 2, "--arch", 6, NULL, (char**)&options.arch)) {
                continue;
            } else if (!__parse_quantity_switch(argv, argc, &i, "-s", 2, "--size", 6, (1024ULL * 1024ULL) * 4096ULL, &options.size)) {
                continue;
            } else if (!__parse_quantity_switch(argv, argc, &i, "-z", 2, "--sector-size", 13, 512, &options.sector_size)) {
                continue;
            } else if (imagePath == NULL) {
                imagePath = argv[i];
            }
        }
    }

    if (options.size == 0) {
        fprintf(stderr, "mkcdk: invalid size provided for image\n");
        return -1;
    }
    if (options.sector_size == 0) {
        fprintf(stderr, "mkcdk: invalid sector-size provided for image\n");
        return -1;
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

    status = __build_image(image, options.out, &options);

cleanup:
    chef_image_destroy(image);
    vlog_cleanup();
    return status;
}
