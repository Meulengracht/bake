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
#include <string.h>
#include <vlog.h>

#include "api.h"
#include "private.h"

#define __MIN(a, b) (((a) < (b)) ? (a) : (b))
#define __BUCKET_SECTOR(b) ((mfs->reserved_sector_count) + (uint64_t)(b * mfs->bucket_size))

struct mfs* mfs_new(struct mfs_new_params* params)
{
    struct mfs* mfs;

    mfs = calloc(1, sizeof(struct mfs));
    if (mfs == NULL) {
        return NULL;
    }

    memcpy(&mfs->ops, &params->ops, sizeof(struct mfs_storage_operations));
    mfs->label = params->label;
    mfs->guid = params->guid;
    mfs->sector_count = params->sector_count;
    mfs->bytes_per_sector = params->bytes_per_sector;
    mfs->sectors_per_track = params->sectors_per_track;
    mfs->heads_per_cylinder = params->heads_per_cylinder;

    if (strcmp(params->guid, "C4483A10-E3A0-4D3F-B7CC-C04A6E16612B") == 0) {
        mfs->flags |= MFS_PARTITION_FLAG_SYSTEMDRIVE;
    } else if (strmcp(params->guid, "80C6C62A-B0D6-4FF4-A69D-558AB6FD8B53") == 0) {
        mfs->flags |= MFS_PARTITION_FLAG_USERDRIVE | MFS_PARTITION_FLAG_DATADRIVE;
    } else if (strmcp(params->guid, "8874F880-E7AD-4EE2-839E-6FFA54F19A72") == 0) {
        mfs->flags |= MFS_PARTITION_FLAG_USERDRIVE;
    } else if (strmcp(params->guid, "B8E1A523-5865-4651-9548-8A43A9C21384") == 0) {
        mfs->flags |= MFS_PARTITION_FLAG_DATADRIVE;
    }
    return mfs;
}

static uint8_t* __new_buffer(struct mfs* mfs, uint32_t sectorCount)
{
    uint8_t* buffer;

    buffer = calloc(sectorCount, mfs->bytes_per_sector);
    if (buffer == NULL) {
        VLOG_FATAL("mfs", "__new_buffer: could not allocate memory for sectors\n");
    }
    return buffer;
}

static uint32_t __checksum(const void* data, size_t length, int skipIndex, int skipLength)
{
    uint32_t       checksum = 0;
    const uint8_t* p = (const uint8_t*)data;

    for (int i = 0; i < length; i++) {
        if (i >= skipIndex && i < (skipIndex + skipLength)) {
            continue;
        }
        checksum += p[i];
    }
    return checksum;
}

void mfs_set_reserved_sectors(struct mfs* mfs, uint16_t count)
{
    mfs->reserved_sector_count = count;
}

static uint16_t __calculate_bucket_size(uint64_t driveSizeBytes)
{
    if (driveSizeBytes <= __GB) {
        return 8;
    } else if (driveSizeBytes <= (64ULL * __GB)) {
        return 16;
    } else if (driveSizeBytes <= (256ULL * __GB)) {
        return 32;
    } else {
        return 64;
    }
}

static int __build_master_record(
    struct mfs* mfs, uint32_t rootBucket, uint32_t journalBucket, uint32_t badListBucket, 
    uint64_t masterRecordSector, uint64_t masterRecordMirrorSector)
{
    uint8_t* masterRecord;
    int      status;

    // Build a new master-record structure
    //uint32_t Magic;
    //uint32_t Flags;
    //uint32_t Checksum;      // Checksum of the master-record
    //uint8_t PartitionName[64];

    //uint32_t FreeBucket;        // Pointer to first free index
    //uint32_t RootIndex;     // Pointer to root directory
    //uint32_t BadBucketIndex;    // Pointer to list of bad buckets
    //uint32_t JournalIndex;  // Pointer to journal file

    //uint64_t MapSector;     // Start sector of bucket-map_sector
    //uint64_t MapSize;		// Size of bucket map
    masterRecord = __new_buffer(mfs, 1);
    masterRecord[0] = 0x4D;
    masterRecord[1] = 0x46;
    masterRecord[2] = 0x53;
    masterRecord[3] = 0x31;

    // Initialize partition flags
    masterRecord[4] = (uint8_t)(mfs->flags & 0xFF);
    masterRecord[5] = (uint8_t)((mfs->flags >> 8) & 0xFF);

    // Initialize partition name
    memcpy(&masterRecord[12], mfs->label, __MIN(strlen(mfs->label), 64));

    // Initialize free pointer
    uint64_t freeBucket = mfs_bucket_map_next_free(mfs->map);
    masterRecord[76] = (uint8_t)(freeBucket & 0xFF);
    masterRecord[77] = (uint8_t)((freeBucket >> 8) & 0xFF);
    masterRecord[78] = (uint8_t)((freeBucket >> 16) & 0xFF);
    masterRecord[79] = (uint8_t)((freeBucket >> 24) & 0xFF);

    // Initialize root directory pointer
    masterRecord[80] = (uint8_t)(rootBucket & 0xFF);
    masterRecord[81] = (uint8_t)((rootBucket >> 8) & 0xFF);
    masterRecord[82] = (uint8_t)((rootBucket >> 16) & 0xFF);
    masterRecord[83] = (uint8_t)((rootBucket >> 24) & 0xFF);

    // Initialize bad bucket list pointer
    masterRecord[84] = (uint8_t)(badListBucket & 0xFF);
    masterRecord[85] = (uint8_t)((badListBucket >> 8) & 0xFF);
    masterRecord[86] = (uint8_t)((badListBucket >> 16) & 0xFF);
    masterRecord[87] = (uint8_t)((badListBucket >> 24) & 0xFF);

    // Initialize journal list pointer
    masterRecord[88] = (uint8_t)(journalBucket & 0xFF);
    masterRecord[89] = (uint8_t)((journalBucket >> 8) & 0xFF);
    masterRecord[90] = (uint8_t)((journalBucket >> 16) & 0xFF);
    masterRecord[91] = (uint8_t)((journalBucket >> 24) & 0xFF);

    // Initialize map sector pointer
    uint64_t offset = mfs_bucket_map_start_sector(mfs->map);
    masterRecord[92] = (uint8_t)(offset & 0xFF);
    masterRecord[93] = (uint8_t)((offset >> 8) & 0xFF);
    masterRecord[94] = (uint8_t)((offset >> 16) & 0xFF);
    masterRecord[95] = (uint8_t)((offset >> 24) & 0xFF);
    masterRecord[96] = (uint8_t)((offset >> 32) & 0xFF);
    masterRecord[97] = (uint8_t)((offset >> 40) & 0xFF);
    masterRecord[98] = (uint8_t)((offset >> 48) & 0xFF);
    masterRecord[99] = (uint8_t)((offset >> 56) & 0xFF);

    // Initialize map size
    uint64_t mapSize = mfs_bucket_map_size(mfs->map);
    masterRecord[100] = (uint8_t)(mapSize & 0xFF);
    masterRecord[101] = (uint8_t)((mapSize >> 8) & 0xFF);
    masterRecord[102] = (uint8_t)((mapSize >> 16) & 0xFF);
    masterRecord[103] = (uint8_t)((mapSize >> 24) & 0xFF);
    masterRecord[104] = (uint8_t)((mapSize >> 32) & 0xFF);
    masterRecord[105] = (uint8_t)((mapSize >> 40) & 0xFF);
    masterRecord[106] = (uint8_t)((mapSize >> 48) & 0xFF);
    masterRecord[107] = (uint8_t)((mapSize >> 56) & 0xFF);

    // Initialize checksum
    uint32_t checksum = __checksum(masterRecord, 512, 8, 4);
    masterRecord[8] = (uint8_t)(checksum & 0xFF);
    masterRecord[9] = (uint8_t)((checksum >> 8) & 0xFF);
    masterRecord[10] = (uint8_t)((checksum >> 16) & 0xFF);
    masterRecord[11] = (uint8_t)((checksum >> 24) & 0xFF);

    // Flush it to disk
    status = mfs->ops.write(masterRecordSector, masterRecord, 1, mfs->ops.op_context);
    if (status) {
        VLOG_ERROR("mfs", "__build_master_record: failed to write primary record\n");
        free(masterRecord);
        return status;
    }
    status = mfs->ops.write(masterRecordMirrorSector, masterRecord, 1, mfs->ops.op_context);
    if (status) {
        VLOG_ERROR("mfs", "__build_master_record: failed to write primary record\n");
    }
    free(masterRecord);
    return status;
}

static int __build_vbr(struct mfs* mfs, uint64_t masterBucketSector, uint64_t mirrorMasterBucketSector)
{
    // Initialize the MBR
    //uint8_t JumpCode[3];
    //uint32_t Magic;
    //uint8_t Version;
    //uint8_t Flags;
    //uint8_t MediaType;
    //uint16_t SectorSize;
    //uint16_t SectorsPerTrack;
    //uint16_t HeadsPerCylinder;
    //uint64_t SectorCount;
    //uint16_t ReservedSectors;
    //uint16_t SectorsPerBucket;
    //uint64_t MasterRecordSector;
    //uint64_t MasterRecordMirror;
    uint8_t* bootSector = __new_buffer(mfs, 1);
    int      status;

    // Initialize magic
    bootSector[3] = 0x4D;
    bootSector[4] = 0x46;
    bootSector[5] = 0x53;
    bootSector[6] = 0x31;

    // Initialize version
    bootSector[7] = 0x1;

    // Initialize flags
    // 0x1 - BootDrive
    // 0x2 - Encrypted
    bootSector[8] = 0x1;

    // Initialize disk metrics
    bootSector[9] = 0x80;
    bootSector[10] = (uint8_t)(mfs->bytes_per_sector & 0xFF);
    bootSector[11] = (uint8_t)((mfs->bytes_per_sector >> 8) & 0xFF);

    // Sectors per track
    bootSector[12] = (uint8_t)(mfs->sectors_per_track & 0xFF);
    bootSector[13] = (uint8_t)((mfs->sectors_per_track >> 8) & 0xFF);

    // Heads per cylinder
    bootSector[14] = (uint8_t)(mfs->heads_per_cylinder & 0xFF);
    bootSector[15] = (uint8_t)((mfs->heads_per_cylinder >> 8) & 0xFF);

    // Total sectors on partition
    bootSector[16] = (uint8_t)(mfs->sector_count & 0xFF);
    bootSector[17] = (uint8_t)((mfs->sector_count >> 8) & 0xFF);
    bootSector[18] = (uint8_t)((mfs->sector_count >> 16) & 0xFF);
    bootSector[19] = (uint8_t)((mfs->sector_count >> 24) & 0xFF);
    bootSector[20] = (uint8_t)((mfs->sector_count >> 32) & 0xFF);
    bootSector[21] = (uint8_t)((mfs->sector_count >> 40) & 0xFF);
    bootSector[22] = (uint8_t)((mfs->sector_count >> 48) & 0xFF);
    bootSector[23] = (uint8_t)((mfs->sector_count >> 56) & 0xFF);

    // Reserved sectors
    bootSector[24] = (uint8_t)(mfs->reserved_sector_count & 0xFF);
    bootSector[25] = (uint8_t)((mfs->reserved_sector_count >> 8) & 0xFF);

    // Size of an bucket in sectors
    bootSector[26] = (uint8_t)(mfs->bucket_size & 0xFF);
    bootSector[27] = (uint8_t)((mfs->bucket_size >> 8) & 0xFF);

    // Sector of master-record
    bootSector[28] = (uint8_t)(masterBucketSector & 0xFF);
    bootSector[29] = (uint8_t)((masterBucketSector >> 8) & 0xFF);
    bootSector[30] = (uint8_t)((masterBucketSector >> 16) & 0xFF);
    bootSector[31] = (uint8_t)((masterBucketSector >> 24) & 0xFF);
    bootSector[32] = (uint8_t)((masterBucketSector >> 32) & 0xFF);
    bootSector[33] = (uint8_t)((masterBucketSector >> 40) & 0xFF);
    bootSector[34] = (uint8_t)((masterBucketSector >> 48) & 0xFF);
    bootSector[35] = (uint8_t)((masterBucketSector >> 56) & 0xFF);

    // Sector of master-record mirror
    bootSector[36] = (uint8_t)(mirrorMasterBucketSector & 0xFF);
    bootSector[37] = (uint8_t)((mirrorMasterBucketSector >> 8) & 0xFF);
    bootSector[38] = (uint8_t)((mirrorMasterBucketSector >> 16) & 0xFF);
    bootSector[39] = (uint8_t)((mirrorMasterBucketSector >> 24) & 0xFF);
    bootSector[40] = (uint8_t)((mirrorMasterBucketSector >> 32) & 0xFF);
    bootSector[41] = (uint8_t)((mirrorMasterBucketSector >> 40) & 0xFF);
    bootSector[42] = (uint8_t)((mirrorMasterBucketSector >> 48) & 0xFF);
    bootSector[43] = (uint8_t)((mirrorMasterBucketSector >> 56) & 0xFF);

    status = mfs->ops.write(0, bootSector, 1, mfs->ops.op_context);
    if (status) {
        VLOG_ERROR("mfs", "__build_vbr: failed to write vbr\n");
    }
    free(bootSector);
    return status;
}

int mfs_format(struct mfs* mfs)
{
    uint64_t partitionSize;
    uint32_t masterBucketSector, mirrorMasterBucketSector;
    uint32_t initialBucketSize;
    uint32_t rootIndex, journalIndex, badBucketIndex;
    uint8_t* buffer;
    int      status;

    if (mfs->reserved_sector_count == 0) {
        // minimum one for vbr
        mfs->reserved_sector_count = 1;
    }

    partitionSize = mfs->sector_count * mfs->bytes_per_sector;
    VLOG_DEBUG("mfs", "mfs_format: size of partition %llu bytes\n", partitionSize);

    mfs->bucket_size = __calculate_bucket_size(partitionSize);
    masterBucketSector = mfs->reserved_sector_count;

    // round the number of reserved sectors up to a equal of buckets
    mfs->reserved_sector_count = (uint16_t)((((mfs->reserved_sector_count + 1) / mfs->bucket_size) + 1) * mfs->bucket_size);

    VLOG_DEBUG("mfs", "mfs_format: bucket size: %u\n", mfs->bucket_size);
    VLOG_DEBUG("mfs", "mfs_format: reserved sectors: %u\n", mfs->reserved_sector_count);

    mfs->map = mfs_bucket_new(
        &mfs->ops,
        mfs->reserved_sector_count,
        mfs->sector_count - mfs->reserved_sector_count,
        mfs->bucket_size
    );
    if (mfs->map == NULL) {
        return -1;
    }

    mfs_bucket_initialize(mfs->map);

    mirrorMasterBucketSector = mfs_bucket_map_start_sector(mfs->map) - 1;
    VLOG_DEBUG("mfs", "mfs_format: creating master-records\n");
    VLOG_DEBUG("mfs", "mfs_format: original: %llu\n", masterBucketSector);
    VLOG_DEBUG("mfs", "mfs_format: mirror: %llu\n", mirrorMasterBucketSector);

    // Allocate for:
    // - Root directory - 8 buckets
    // - Bad-bucket list - 1 bucket
    // - Journal list - 8 buckets
    rootIndex = mfs_bucket_map_allocate(mfs->map, 8, &initialBucketSize);
    journalIndex = mfs_bucket_map_allocate(mfs->map, 8, &initialBucketSize);
    badBucketIndex = mfs_bucket_map_allocate(mfs->map, 1, &initialBucketSize);
    VLOG_DEBUG("mfs", "mfs_format: free bucket pointer after setup: %u\n", mfs_bucket_map_next_free(mfs->map));
    VLOG_DEBUG("mfs", "mfs_format: wiping root data\n");

    // Allocate a zero array to fill the allocated sectors with
    buffer = __new_buffer(mfs, mfs->bucket_size);
    status = mfs->ops.write(__BUCKET_SECTOR(badBucketIndex), buffer, mfs->bucket_size, mfs->ops.op_context);
    free(buffer);
    if (status) {
        VLOG_ERROR("mfs", "mfs_format: failed to wipe bad-buckets bucket\n");
        return status;
    }

    buffer = __new_buffer(mfs, mfs->bucket_size * 8);
    status = mfs->ops.write(__BUCKET_SECTOR(rootIndex), buffer, mfs->bucket_size * 8, mfs->ops.op_context);
    if (status) {
        VLOG_ERROR("mfs", "mfs_format: failed to wipe root bucket\n");
        free(buffer);
        return status;
    }
    
    status = mfs->ops.write(__BUCKET_SECTOR(journalIndex), buffer, mfs->bucket_size * 8, mfs->ops.op_context);
    if (status) {
        VLOG_ERROR("mfs", "mfs_format: failed to wipe journal bucket\n");
        free(buffer);
        return status;
    }
    free(buffer);

    // build master record
    VLOG_DEBUG("mfs", "mfs_format: installing master records\n");
    status = __build_master_record(mfs, rootIndex, journalIndex, badBucketIndex, masterBucketSector, mirrorMasterBucketSector);
    if (status) {
        VLOG_ERROR("mfs", "mfs_format: failed to build and write master record\n");
        return status;
    }

    // install vbr
    VLOG_DEBUG("mfs", "mfs_format: installing vbr\n");
    status = __build_vbr(mfs, masterBucketSector, mirrorMasterBucketSector);
    if (status) {
        VLOG_ERROR("mfs", "mfs_format: failed to build and write virtual boot record\n");
        return status;
    }
    return 0;
}
