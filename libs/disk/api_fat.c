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

#include <chef/platform.h>
#include <stdlib.h>
#include <string.h>
#include <vlog.h>

#include "private.h"
#include "filesystems/fat/fat_filelib.h"

struct __fat_filesystem {
    struct chef_disk_filesystem        base;
    struct chef_filesystem_fat_options options;
    struct fatfs*                      fs;
    const char*                        label;
    const char*                        content;
    uint64_t                           sector_count;
    uint16_t                           bytes_per_sector;
    FILE*                              stream;
};

static int __update_mbr(struct __fat_filesystem* cfs, uint8* sector)
{
    struct platform_stat stats;
    char                 tmp[PATH_MAX];
    void*                buffer;
    size_t               size;
    int                  status;

    if (cfs->content == NULL) {
        return 0;
    }

    snprintf(
        &tmp[0], sizeof(tmp) -1,
        "%s" CHEF_PATH_SEPARATOR_S "resources" CHEF_PATH_SEPARATOR_S "mbr.img",
        cfs->content
    );
    if (platform_stat(&tmp[0], &stats)) {
        // not there, ignore
        return 0;
    }

    status = platform_readfile(&tmp[0], &buffer, &size);
    if (status) {
        VLOG_ERROR("fat", "__update_mbr: failed to read %s\n", &tmp[0]);
        return status;
    }
    if (size != 512) {
        VLOG_ERROR("fat", "__update_mbr: %s is not correctly sized\n", &tmp[0]);
        free(buffer);
        return -1;
    }

    // 0-2     - Jump code
    // 3-61    - EBPB
    // 62-509  - Boot code
    // 510-511 - Boot signature
    memcpy(&sector[0], &((uint8_t*)buffer)[0], 3);
    memcpy(&sector[62], &((uint8_t*)buffer)[62], 448);
    sector[510] = 0x55;
    sector[511] = 0xAA;

    free(buffer);
    return 0;
}

static int __write_reserved_image(struct __fat_filesystem* cfs)
{
    struct platform_stat stats;
    char                 tmp[PATH_MAX];
    void*                buffer;
    size_t               size, written;
    int                  status;

    if (cfs->content == NULL && cfs->options.reserved_image == NULL) {
        return 0;
    }

    if (cfs->content != NULL) {
        snprintf(
            &tmp[0], sizeof(tmp) -1,
            "%s" CHEF_PATH_SEPARATOR_S "resources" CHEF_PATH_SEPARATOR_S "fat.img",
            cfs->content
        );
        if (platform_stat(&tmp[0], &stats)) {
            // not there, ignore
            return 0;
        }
    } else {
        strcpy(&tmp[0], cfs->options.reserved_image);
    }

    status = platform_readfile(&tmp[0], &buffer, &size);
    if (status) {
        VLOG_ERROR("fat", "__write_reserved_image: failed to read %s\n", &tmp[0]);
        return status;
    }

    written = fwrite(buffer, size, 1, cfs->stream);
    if (written != size) {
        VLOG_ERROR("fat", "__write_reserved_image: failed to write reserved sectors\n");
        free(buffer);
        return -1;
    }

    free(buffer);
    return 0;
}

static int __partition_read(uint32 sector, uint8 *buffer, uint32 sector_count, void* ctx)
{
    struct __fat_filesystem* cfs = ctx;
    uint64_t                 offset;
    int                      status;

    // make sure we get the stream position
    offset = sector * cfs->bytes_per_sector;
    VLOG_DEBUG("fat", "__partition_read: seek to %llu\n", offset);
    status = fseek(cfs->stream, (long)offset, SEEK_SET);

    fread(buffer, cfs->bytes_per_sector, sector_count, cfs->stream);
    return 0;
}

static int __partition_write(uint32 sector, uint8 *buffer, uint32 sector_count, void* ctx)
{
    struct __fat_filesystem* cfs = ctx;
    uint64_t                 offset;
    int                      status;

    // make sure we get the stream position
    offset = sector * cfs->bytes_per_sector;
    VLOG_DEBUG("fat", "__partition_write: seek to %llu\n", offset);
    status = fseek(cfs->stream, offset, SEEK_SET);

    // if the sector is 0, then let us modify the boot sector with the
    // MBR provided by content
    if (sector == 0 && cfs->content != NULL) {
        status = __update_mbr(cfs, buffer);
        if (status) {
            return status;
        }
    }

    fwrite(buffer, cfs->bytes_per_sector, sector_count, cfs->stream);

    // let us write the reserved image contents
    // at the same time
    if (sector == 0) {
        status = __write_reserved_image(cfs);
        if (status) {
            return status;
        }
    }
    return 0;
}

static void __fs_set_content(struct chef_disk_filesystem* fs, const char* path)
{
    struct __fat_filesystem* cfs = (struct __fat_filesystem*)fs;
    cfs->content = path;
}

static int __fs_format(struct chef_disk_filesystem* fs)
{
    struct __fat_filesystem* cfs = (struct __fat_filesystem*)fs;
    if (cfs->content != NULL || cfs->options.reserved_image != NULL) {
        struct platform_stat stats;
        char                 tmp[PATH_MAX];

        if (cfs->content != NULL) {
            snprintf(
                &tmp[0], sizeof(tmp) -1,
                "%s" CHEF_PATH_SEPARATOR_S "resources" CHEF_PATH_SEPARATOR_S "fat.img",
                cfs->content
            );
        } else {
            strcpy(&tmp[0], cfs->options.reserved_image);
        }
        if (!platform_stat(&tmp[0], &stats)) {
            cfs->fs->reserved_sectors = (stats.size / cfs->bytes_per_sector) + 1;
        }
    }
    return fl_format(cfs->fs, (uint32_t)cfs->sector_count, cfs->label) == 0 ? -1 : 0;
}

static int __fs_create_directory(struct chef_disk_filesystem* fs, struct chef_disk_fs_create_directory_params* params)
{
    struct __fat_filesystem* cfs = (struct __fat_filesystem*)fs;
    return fl_createdirectory(cfs->fs, params->path) == 0 ? -1 : 0;
}

static int __fs_create_file(struct chef_disk_filesystem* fs, struct chef_disk_fs_create_file_params* params)
{
    struct __fat_filesystem* cfs = (struct __fat_filesystem*)fs;
    FL_FILE*                 stream;
    int                      written;

    stream = fl_fopen(cfs->fs, params->path, "w");
    if (stream == NULL) {
        return -1;
    }

    written = fl_fwrite(cfs->fs, params->buffer, 1, params->size, stream);
    if (written != params->size) {
        fl_fclose(cfs->fs, stream);
        return -1;
    }

    fl_fclose(cfs->fs, stream);
    return 0;
}

static int __fs_write_raw(struct chef_disk_filesystem* fs, struct chef_disk_fs_write_raw_params* params)
{
    struct __fat_filesystem* cfs = (struct __fat_filesystem*)fs;
    if (params->sector == 0) {
        uint8_t mbr[512]; // ehh should be cfs->bytes_per_sector
        int     status;

        // we are writing MBR, fix it up
        if (params->size != cfs->bytes_per_sector) {
            return -1;
        }

        status = __partition_read(0, &mbr[0], 1, cfs);
        if (status) {
            VLOG_ERROR("fat", "failed to read mbr from partition\n");
            return status;
        }

        // 0-2     - Jump code
        // 3-61    - EBPB
        // 62-509  - Boot code
        // 510-511 - Boot signature
        memcpy(&mbr[0], &((uint8_t*)params->buffer)[0], 3);
        memcpy(&mbr[62], &((uint8_t*)params->buffer)[62], 448);
        mbr[510] = 0x55;
        mbr[511] = 0xAA;
        return __partition_write((uint32_t)params->sector, &mbr[0], 1, cfs);
    } else {
        return __partition_write((uint32_t)params->sector, (uint8_t*)params->buffer, params->size / cfs->bytes_per_sector, cfs);
    }
}

static int __fs_finish(struct chef_disk_filesystem* fs)
{
    struct __fat_filesystem* cfs = (struct __fat_filesystem*)fs;
    fl_delete(cfs->fs);
    free(cfs);
    return 0;
}

struct chef_disk_filesystem* chef_filesystem_fat32_new(struct chef_disk_partition* partition, struct chef_disk_filesystem_params* params)
{
    struct __fat_filesystem* cfs;
    int                      status;
    VLOG_DEBUG("fat", "chef_filesystem_fat32_new(partition=%s)\n", partition->name);

    cfs = calloc(1, sizeof(struct __fat_filesystem));
    if (cfs == NULL) {
        VLOG_ERROR("fat", "chef_filesystem_fat32_new: failed to allocate memory\n");
        return NULL;
    }

    cfs->fs = fl_new();
    if (cfs->fs == NULL) {
        VLOG_ERROR("fat", "chef_filesystem_fat32_new: failed to create new FAT instance\n");
        free(cfs);
        return NULL;
    }

    // store members from partition that we need later
    cfs->label = partition->name;
    cfs->bytes_per_sector = params->sector_size;
    cfs->sector_count = partition->sector_count;
    cfs->stream = partition->stream;

    // copy options
    cfs->options.reserved_image = params->options.fat.reserved_image;

    // install operations
    cfs->base.set_content = __fs_set_content;
    cfs->base.format = __fs_format;
    cfs->base.create_directory = __fs_create_directory;
    cfs->base.create_file = __fs_create_file;
    cfs->base.write_raw = __fs_write_raw;
    cfs->base.finish = __fs_finish;

    // prepare for formatting
    cfs->fs->disk_io.read_media = __partition_read;
    cfs->fs->disk_io.write_media = __partition_write;
    cfs->fs->disk_io.user_ctx = cfs;
    return &cfs->base;
}
