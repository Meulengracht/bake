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

#ifndef __PRIVATE_H__
#define __PRIVATE_H__

#include <stdint.h>

#define MFS_TYPE 0x61

#define MFS_ENDOFCHAIN 0xFFFFFFFFUL
#define MFS_RECORDSIZE 1024
#define MFS_EXPANDSIZE 8
#define MAPENTRY_SIZE  8

#define __KB 1024
#define __MB (__KB * 1024)
#define __GB (__MB * 1024)

enum mfs_partition_flags
{
    MFS_PARTITION_FLAG_SYSTEMDRIVE  = 0x1,
    MFS_PARTITION_FLAG_DATADRIVE    = 0x2,
    MFS_PARTITION_FLAG_USERDRIVE    = 0x4,
    MFS_PARTITION_FLAG_HIDDENDRIVE  = 0x8,
    MFS_PARTITION_FLAG_JOURNALCHECK = 0x10
};

// File flags for mfs file entries
//uint32_t Flags;             // 0x00 - Record Flags
//uint32_t StartBucket;       // 0x04 - First data bucket
//uint32_t StartLength;       // 0x08 - Length of first data bucket
//uint32_t RecordChecksum;        // 0x0C - Checksum of record excluding this entry + inline data
//uint64_t DataChecksum;      // 0x10 - Checksum of data
//DateTimeRecord_t CreatedAt;         // 0x18 - Created timestamp
//DateTimeRecord_t ModifiedAt;            // 0x20 - Last modified timestamp
//DateTimeRecord_t AccessedAt;            // 0x28 - Last accessed timestamp
//uint64_t Size;              // 0x30 - Size of data (Set size if sparse)
//uint64_t AllocatedSize;     // 0x38 - Actual size allocated
//uint32_t SparseMap;         // 0x40 - Bucket of sparse-map
//uint8_t Name[300];          // 0x44 - Record name (150 UTF16)
//VersionRecord_t Versions[4];// 0x170 - Record Versions
//uint8_t Integrated[512];	// 0x200

/**
 * @brief File record cache structure
 *        Represenst a file-entry in cached format 
 */
struct mfs_record {
    const char*           name;
    enum mfs_record_flags flags;
    uint64_t              size;
    uint64_t              allocated_size;
    uint32_t              bucket;
    uint32_t              bucket_length;

    // used for record tracking
    uint32_t              directory_bucket;
    uint32_t              directory_length;
    uint32_t              directory_index;
};

struct mfs {
    struct mfs_storage_operations ops;
    struct mfs_bucket_map*        map;

    const char* label;
    const char* guid;
    uint16_t    bytes_per_sector;
    uint16_t    sectors_per_track;
    uint16_t    heads_per_cylinder;
    uint64_t    sector_count;
    uint16_t    bucket_size;
    uint16_t    reserved_sector_count;
    uint32_t    flags;
};

struct mfs_bucket_map;

extern struct mfs_bucket_map* mfs_bucket_new(struct mfs_storage_operations* ops, uint64_t sector, uint32_t sectorCount, uint16_t sectorsPerBucket);

extern uint32_t mfs_bucket_map_next_free(struct mfs_bucket_map* map);
extern uint64_t mfs_bucket_map_start_sector(struct mfs_bucket_map* map);
extern uint64_t mfs_bucket_map_size(struct mfs_bucket_map* map);

extern void mfs_bucket_initialize(struct mfs_bucket_map* map);
extern void mfs_bucket_open(struct mfs_bucket_map* map, uint64_t mapSector, uint32_t nextFreeBucket);

/**
 * @brief Link of <bucket> is returned as result, length of <bucket> is provided in <length>
 */
extern uint32_t mfs_bucket_map_bucket_info(struct mfs_bucket_map* map, uint32_t bucket, uint32_t* length);

/**
 * @brief Updates the link to the next bucket for the given bucket
 */
extern void mfs_bucket_map_set_bucket_link(struct mfs_bucket_map* map, uint32_t bucket, uint32_t nextBucket);

/**
 * @brief Allocates new buckets from the bucket map
 */
extern uint32_t mfs_bucket_map_allocate(struct mfs_bucket_map* map, uint32_t bucketCount, uint32_t* sizeOfFirstBucket);

#endif //!__PRIVATE_H__
