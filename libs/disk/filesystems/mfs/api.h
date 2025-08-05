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

#ifndef __MFS_LIBRARY_API_H__
#define __MFS_LIBRARY_API_H__

#include <stdint.h>

struct mfs;

struct mfs_storage_operations {
    int (*read)(uint64_t sector, uint8_t *buffer, uint32_t sector_count, void* ctx);
    int (*write)(uint64_t sector, uint8_t *buffer, uint32_t sector_count, void* ctx);
    void* op_context;
};

struct mfs_new_params {
    struct mfs_storage_operations ops;
    const char*                   label;
    const char*                   guid;
    uint64_t                      sector_count;
    uint16_t                      bytes_per_sector;
    uint16_t                      sectors_per_track;
    uint16_t                      heads_per_cylinder;
};

extern struct mfs* mfs_new(struct mfs_new_params* params);
extern void mfs_delete(struct mfs* mfs);

extern void mfs_set_reserved_sectors(struct mfs* mfs, uint16_t count);
extern int  mfs_format(struct mfs* mfs);

enum mfs_record_flags
{
    MFS_RECORD_FLAG_DIRECTORY = 0x1,
    MFS_RECORD_FLAG_LINK      = 0x2,
    MFS_RECORD_FLAG_SECURITY  = 0x4,
    MFS_RECORD_FLAG_SYSTEM    = 0x8,
    MFS_RECORD_FLAG_HIDDEN    = 0x10,
    MFS_RECORD_FLAG_CHAINED   = 0x20,
    MFS_RECORD_FLAG_LOCKED    = 0x40,
    MFS_RECORD_FLAG_VERSIONED = 0x10000000,
    MFS_RECORD_FLAG_INLINE    = 0x20000000,
    MFS_RECORD_FLAG_SPARSE    = 0x40000000,
    MFS_RECORD_FLAG_INUSE     = 0x80000000
};

extern int mfs_create_file(struct mfs* mfs, const char* path, enum mfs_record_flags flags, uint8_t* fileContents, size_t length);
extern int mfs_create_directory(struct mfs* mfs, const char* path);

#endif //!__MFS_LIBRARY_API_H__
