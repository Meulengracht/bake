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

#include <stdlib.h>

#include "private.h"
#include "filesystems/fat/fat_filelib.h"

struct __fat_filesystem {
    struct chef_disk_filesystem base;
    struct fatfs*               fs;
    uint16_t                    bytes_per_sector;
    FILE*                       stream;
};

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

    fwrite(buffer, cfs->bytes_per_sector, sector_count, cfs->stream);
    return 0;
}

static int __fs_set_content(struct chef_disk_filesystem* fs, struct ingredient* ig)
{
    struct __fat_filesystem* cfs = (struct __fat_filesystem*)fs;

}

static int __fs_format(struct chef_disk_filesystem* fs, uint64_t sectorCount, const char* name)
{
    struct __fat_filesystem* cfs = (struct __fat_filesystem*)fs;
    return fl_format(cfs->fs, (uint32_t)sectorCount, name);
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

    // install operations
    cfs->base.set_content = __fs_set_content;
    cfs->base.format = __fs_format;
    cfs->base.finish = __fs_finish;
}
