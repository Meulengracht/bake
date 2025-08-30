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
#include "filesystems/mfs/api.h"

struct __mfs_filesystem {
    struct chef_disk_filesystem base;
    struct mfs*                 fs;
    const char*                 label;
    const char*                 content;
    uint64_t                    sector_count;
    uint16_t                    bytes_per_sector;
    FILE*                       stream;
};

static int __update_mbr(struct __mfs_filesystem* cfs, uint8_t* sector)
{
    struct platform_stat stats;
    char                 tmp[PATH_MAX];
    void*                buffer;
    size_t               size;
    int                  status;

    // must have content set
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
        VLOG_ERROR("mfs", "__update_mbr: failed to read %s\n", &tmp[0]);
        return status;
    }
    if (size != 512) {
        VLOG_ERROR("mfs", "__update_mbr: %s is not correctly sized\n", &tmp[0]);
        free(buffer);
        return -1;
    }

    // 0-2     - Jump code
    // 3-43    - Header
    // 44-509  - Boot code
    // 510-511 - Boot signature
    memcpy(&sector[0], &((uint8_t*)buffer)[0], 3);
    memcpy(&sector[44], &((uint8_t*)buffer)[44], 465);
    sector[8] = 0x1; // mark OS partition
    sector[510] = 0x55;
    sector[511] = 0xAA;

    free(buffer);
    return 0;
}

static int __write_reserved_image(struct __mfs_filesystem* cfs)
{
    struct platform_stat stats;
    char                 tmp[PATH_MAX];
    void*                buffer;
    size_t               size, written;
    int                  status;

    // must have content set
    if (cfs->content == NULL) {
        return 0;
    }

    snprintf(
        &tmp[0], sizeof(tmp) -1,
        "%s" CHEF_PATH_SEPARATOR_S "resources" CHEF_PATH_SEPARATOR_S "mfs.img",
        cfs->content
    );
    if (platform_stat(&tmp[0], &stats)) {
        // not there, ignore
        return 0;
    }

    status = platform_readfile(&tmp[0], &buffer, &size);
    if (status) {
        VLOG_ERROR("mfs", "__update_mbr: failed to read %s\n", &tmp[0]);
        return status;
    }

    written = fwrite(buffer, size, 1, cfs->stream);
    if (written != size) {
        VLOG_ERROR("mfs", "__update_mbr: failed to write reserved sectors\n");
        free(buffer);
        return -1;
    }

    free(buffer);
    return 0;
}

static int __partition_read(uint64_t sector, uint8_t *buffer, uint32_t sector_count, void* ctx)
{
    struct __mfs_filesystem* cfs = ctx;
    uint64_t                 offset;
    int                      status;

    // make sure we get the stream position
    offset = sector * cfs->bytes_per_sector;

    status = fseek(cfs->stream, offset, SEEK_SET);

    fread(buffer, cfs->bytes_per_sector, sector_count, cfs->stream);
    return 0;
}

static int __partition_write(uint64_t sector, uint8_t *buffer, uint32_t sector_count, void* ctx)
{
    struct __mfs_filesystem* cfs = ctx;
    uint64_t                 offset;
    int                      status;

    // make sure we get the stream position
    offset = sector * cfs->bytes_per_sector;
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
    if (sector == 0 && cfs->content != NULL) {
        status = __write_reserved_image(cfs);
        if (status) {
            return status;
        }
    }
    return 0;
}

static void __fs_set_content(struct chef_disk_filesystem* fs, const char* path)
{
    struct __mfs_filesystem* cfs = (struct __mfs_filesystem*)fs;
    cfs->content = path;
}

static int __fs_format(struct chef_disk_filesystem* fs)
{
    struct __mfs_filesystem* cfs = (struct __mfs_filesystem*)fs;
    if (cfs->content != NULL) {
        struct platform_stat stats;
        char                 tmp[PATH_MAX];

        snprintf(
            &tmp[0], sizeof(tmp) -1,
            "%s" CHEF_PATH_SEPARATOR_S "resources" CHEF_PATH_SEPARATOR_S "mfs.img",
            cfs->content
        );
        if (!platform_stat(&tmp[0], &stats)) {
            mfs_set_reserved_sectors(cfs->fs, ((stats.size + (cfs->bytes_per_sector - 1)) / cfs->bytes_per_sector) + 1 /* 1 for VBR */);
        }
    }
    return mfs_format(cfs->fs);
}

static int __fs_create_directory(struct chef_disk_filesystem* fs, struct chef_disk_fs_create_directory_params* params)
{
    struct __mfs_filesystem* cfs = (struct __mfs_filesystem*)fs;
    return mfs_create_directory(cfs->fs, params->path);
}

static int __fs_create_file(struct chef_disk_filesystem* fs, struct chef_disk_fs_create_file_params* params)
{
    struct __mfs_filesystem* cfs = (struct __mfs_filesystem*)fs;
    return mfs_create_file(cfs->fs, params->path, 0, (uint8_t*)params->buffer, params->size);
}

static int __fs_write_raw(struct chef_disk_filesystem* fs, struct chef_disk_fs_write_raw_params* params)
{
    struct __mfs_filesystem* cfs = (struct __mfs_filesystem*)fs;
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
        // 3-43    - Header
        // 44-509  - Boot code
        // 510-511 - Boot signature
        memcpy(&mbr[0], &((uint8_t*)params->buffer)[0], 3);
        memcpy(&mbr[44], &((uint8_t*)params->buffer)[44], 465);
        mbr[8] = 0x1; // mark OS partition
        mbr[510] = 0x55;
        mbr[511] = 0xAA;
        return __partition_write(params->sector, &mbr[0], 1, cfs);
    } else {
        return __partition_write(params->sector, (uint8_t*)params->buffer, params->size / cfs->bytes_per_sector, cfs);
    }
}

static int __fs_finish(struct chef_disk_filesystem* fs)
{
    struct __mfs_filesystem* cfs = (struct __mfs_filesystem*)fs;
    mfs_delete(cfs->fs);
    free(cfs);
    return 0;
}

struct chef_disk_filesystem* chef_filesystem_mfs_new(struct chef_disk_partition* partition, struct chef_disk_filesystem_params* params)
{
    struct __mfs_filesystem* cfs;
    int                      status;
    VLOG_DEBUG("mfs", "chef_filesystem_mfs_new(partition=%s)\n", partition->name);

    cfs = calloc(1, sizeof(struct __mfs_filesystem));
    if (cfs == NULL) {
        VLOG_ERROR("mfs", "chef_filesystem_mfs_new: failed to allocate memory\n");
        return NULL;
    }

    cfs->fs = mfs_new(&(struct mfs_new_params) {
        .ops = {
            .op_context = cfs,
            .read = __partition_read,
            .write = __partition_write
        },
        .label = partition->name,
        .guid = partition->guid,
        .sector_count = partition->sector_count,
        .bytes_per_sector = params->sector_size,
        .heads_per_cylinder = 0,
        .sectors_per_track = 0
    });
    if (cfs->fs == NULL) {
        VLOG_ERROR("mfs", "chef_filesystem_mfs_new: failed to create new FAT instance\n");
        free(cfs);
        return NULL;
    }

    // store members from partition that we need later
    cfs->label = partition->name;
    cfs->bytes_per_sector = params->sector_size;
    cfs->sector_count = partition->sector_count;
    cfs->stream = partition->stream;

    // install operations
    cfs->base.set_content = __fs_set_content;
    cfs->base.format = __fs_format;
    cfs->base.create_directory = __fs_create_directory;
    cfs->base.create_file = __fs_create_file;
    cfs->base.write_raw = __fs_write_raw;
    cfs->base.finish = __fs_finish;
    return &cfs->base;
}
