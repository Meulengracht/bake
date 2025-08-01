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

#include "private.h"
#include "filesystems/fat/fat_filelib.h"

struct __fat_filesystem {
    struct chef_disk_filesystem base;
    struct fatfs*               fs;
    const char*                 label;
    const char*                 content;
    uint64_t                    sector_count;
    uint16_t                    bytes_per_sector;
    FILE*                       stream;
};

static int __update_mbr(struct __fat_filesystem* cfs, uint8* sector)
{
    // look for resources/mbr.img
    

    return 0;
}

static int __write_reserved_image(struct __fat_filesystem* cfs)
{
    // look for resources/fat.img

    return 0;
}

static int __partition_read(uint32 sector, uint8 *buffer, uint32 sector_count, void* ctx)
{
    struct __fat_filesystem* cfs = ctx;
    uint64_t                 offset;
    int                      status;

    // make sure we get the stream position
    offset = sector * cfs->bytes_per_sector;

    status = fseek(cfs->stream, offset, SEEK_SET);

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
    struct __fat_filesystem* cfs = (struct __fat_filesystem*)fs;
    cfs->content = path;
}

static int __fs_format(struct chef_disk_filesystem* fs)
{
    struct __fat_filesystem* cfs = (struct __fat_filesystem*)fs;
    return fl_format(cfs->fs, (uint32_t)cfs->sector_count, cfs->label);
}

static int __fs_create_directory(struct chef_disk_filesystem* fs, struct chef_disk_fs_create_directory_params* params)
{
    struct __fat_filesystem* cfs = (struct __fat_filesystem*)fs;
    return fl_createdirectory(cfs->fs, params->path);
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

static int __fs_finish(struct chef_disk_filesystem* fs)
{
    struct __fat_filesystem* cfs = (struct __fat_filesystem*)fs;
    fl_delete(cfs->fs);
    free(fs);
    return 0;
}

struct chef_disk_filesystem* chef_filesystem_fat32_new(struct chef_disk_partition* partition)
{
    struct __fat_filesystem* cfs;
    int                      status;

    cfs = calloc(1, sizeof(struct __fat_filesystem));
    if (cfs == NULL) {
        return NULL;
    }

    cfs->fs = fl_new();
    if (cfs->fs == NULL) {
        free(cfs);
        return NULL;
    }

    status = fl_attach_media(cfs->fs, __partition_read, __partition_write, cfs);
    if (status) {
        fl_delete(cfs->fs);
        free(cfs);
        return NULL;
    }

    // calculate reserved sector size
    // TODO:

    // store members from partition that we need later
    cfs->label = partition->name;
    cfs->bytes_per_sector = 0; // TODO
    cfs->sector_count = partition->sector_count;

    // install operations
    cfs->base.set_content = __fs_set_content;
    cfs->base.format = __fs_format;
    cfs->base.create_directory = __fs_create_directory;
    cfs->base.create_file = __fs_create_file;
    cfs->base.finish = __fs_finish;
}
