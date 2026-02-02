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
#include <vlog.h>

#include "api.h"
#include "private.h"

struct mfs_bucket_map {
    struct mfs_storage_operations* ops;
    uint32_t                       bytes_per_sector;
    uint64_t                       sector;
    uint32_t                       sector_count;
    uint16_t                       sectors_per_bucket;
    uint64_t                       map_sector;
    uint32_t                       next_free_bucket;
};

struct mfs_bucket_map* mfs_bucket_new(struct mfs_storage_operations* ops, uint64_t sector, uint32_t sectorCount, uint16_t sectorsPerBucket)
{
    struct mfs_bucket_map* map;

    map = calloc(1, sizeof(struct mfs_bucket_map));
    if (map == NULL) {
        return NULL;
    }

    map->ops = ops;
    map->sector = sector;
    map->sector_count = sectorCount;
    map->sectors_per_bucket = sectorsPerBucket;
    return map;
}

void mfs_bucket_delete(struct mfs_bucket_map* map)
{
    if (map == NULL) {
        return;
    }
    free(map);
}

uint32_t mfs_bucket_map_next_free(struct mfs_bucket_map* map)
{
    return map->next_free_bucket;
}

uint64_t mfs_bucket_map_start_sector(struct mfs_bucket_map* map)
{
    return map->map_sector;
}

uint64_t mfs_bucket_map_size(struct mfs_bucket_map* map)
{
    return (map->sector_count / map->sectors_per_bucket) * (uint64_t)MAPENTRY_SIZE;
}

// length is upper dword, link is lower dword
static void __write_entry_to_buffer(uint32_t* buffer, uint32_t index, uint32_t length, uint32_t nextBucket)
{
    buffer[index*2] = nextBucket;
    buffer[(index*2)+1] = length;
}

void mfs_bucket_initialize(struct mfs_bucket_map* map)
{
    // Start by calculating the maximum size of the map. 
    // MasterBucket | Data | MasterBucketMirror | Map
    uint64_t maxMapSize = mfs_bucket_map_size(map); // Bytes
    uint64_t mapSectorCount = (maxMapSize + (map->bytes_per_sector - 1)) / map->bytes_per_sector; // Sectors
    uint32_t mapBucketCount = (uint32_t)((map->sector_count - mapSectorCount) / map->sectors_per_bucket); // Upper bound of the map
    VLOG_DEBUG("mfs-bucket-map", "mfs_bucket_initialize()\n");

    map->map_sector = (map->sector + map->sector_count - 1) - mapSectorCount;

    // Reserve an additional bucket for the MasterBucketMirror
    mapBucketCount--; 

    VLOG_DEBUG("mfs-bucket-map", "start-sector: %llu\n", map->map_sector);
    VLOG_DEBUG("mfs-bucket-map", "start-size: %llu\n", maxMapSize);
    VLOG_DEBUG("mfs-bucket-map", "available buckets: %u\n", mapBucketCount);

    // A map entry consists of the length of the bucket, and it's link
    // To get the length of the link, you must lookup it's length by accessing Map[Link]
    // Length of bucket 0 is HIDWORD(Map[0]), Link of bucket 0 is LODWORD(Map[0])
    // If the link equals 0xFFFFFFFF there is no link
    uint8_t* sector = calloc(1, map->bytes_per_sector);
    if (sector == NULL) {
        VLOG_FATAL("mfs-bucket-map", "mfs_bucket_initialize: no memory for sector buffer\n");
    }
    __write_entry_to_buffer((uint32_t*)sector, 0, mapBucketCount, MFS_ENDOFCHAIN);
    if (map->ops->write(map->map_sector, sector, 1, map->ops->op_context)) {
        VLOG_FATAL("mfs-bucket-map", "mfs_bucket_initialize: failed to write map sector\n");
    }
    free(sector);
}

void mfs_bucket_open(struct mfs_bucket_map* map, uint64_t mapSector, uint32_t nextFreeBucket)
{
    map->map_sector = mapSector;
    map->next_free_bucket = nextFreeBucket;
}

static uint32_t* __read_sector(struct mfs_bucket_map* map, uint64_t sector, uint32_t count)
{
    uint32_t* buffer;
    int       status;

    buffer = malloc(count * map->bytes_per_sector);
    if (buffer == NULL) {
        VLOG_FATAL("mfs-bucket-map", "__read_sector: failed to allocate memory for buffer\n");
    }

    status = map->ops->read(map->map_sector + sector, (uint8_t*)buffer, count, map->ops->op_context);
    if (status) {
        VLOG_FATAL("mfs-bucket-map", "__read_sector: failed to read sector\n");
    }
    return buffer;
}

uint32_t mfs_bucket_map_allocate(struct mfs_bucket_map* map, uint32_t bucketCount, uint32_t* sizeOfFirstBucket)
{
    if (bucketCount == 0) {
        *sizeOfFirstBucket = 0;
        return MFS_ENDOFCHAIN;
    }

    uint32_t mapEntriesPerSector = map->bytes_per_sector / 8;
    uint32_t allocation = map->next_free_bucket;

    uint32_t bucketsLeft = bucketCount;
    uint32_t bucketLink = map->next_free_bucket; // we start at the free one
    uint32_t bucketLinkPrevious = MFS_ENDOFCHAIN;
    uint32_t firstFreeSize = 0;

    while (bucketsLeft > 0) {
        uint32_t  sizeOfBucket = 0;
        uint32_t  sectorOffset = bucketLink / mapEntriesPerSector; // entry offset (sector)
        uint32_t  sectorIndex = bucketLink % mapEntriesPerSector;  // entry offset (in-sector)
        uint32_t* sectorBuffer = __read_sector(map, sectorOffset, 1);

        bucketLinkPrevious = bucketLink;
        bucketLink = sectorBuffer[sectorIndex * 2]; // link is lower DWORD
        sizeOfBucket = sectorBuffer[(sectorIndex * 2) + 1]; // length of bucket is upper DWORD

        // Did this block have enough for us?
        if (sizeOfBucket > bucketsLeft) {
            // Yes, we need to split it up to two blocks now
            uint32_t nextFreeBucket = bucketLinkPrevious + bucketsLeft;
            uint32_t nextFreeCount = sizeOfBucket - bucketsLeft;

            if (firstFreeSize == 0) {
                firstFreeSize = bucketsLeft;
            }

            // We have to adjust now, since we are taking only a chunk
            // of the available length.
            // bucketsLeft = size we allocate.
            __write_entry_to_buffer(sectorBuffer, sectorIndex, bucketsLeft, MFS_ENDOFCHAIN);
            if (map->ops->write(map->map_sector + sectorOffset, (uint8_t*)sectorBuffer, 1, map->ops->op_context)) {
                VLOG_FATAL("mfs-bucket-map", "mfs_bucket_map_allocate: failed to write map sector\n");
            }

            // Create new block at the next link. Recalculate position
            sectorOffset = nextFreeBucket / mapEntriesPerSector;
            sectorIndex = nextFreeBucket % mapEntriesPerSector;

            // Update the map entry
            if (map->ops->read(map->map_sector + sectorOffset, (uint8_t*)sectorBuffer, 1, map->ops->op_context)) {
                VLOG_FATAL("mfs-bucket-map", "mfs_bucket_map_allocate: failed to read map sector\n");
            }
            __write_entry_to_buffer(sectorBuffer, sectorIndex, nextFreeCount, bucketLink);
            if (map->ops->write(map->map_sector + sectorOffset, (uint8_t*)sectorBuffer, 1, map->ops->op_context)) {
                VLOG_FATAL("mfs-bucket-map", "mfs_bucket_map_allocate: failed to write map sector\n");
            }
            free(sectorBuffer);

            map->next_free_bucket = nextFreeBucket;
            *sizeOfFirstBucket = firstFreeSize;
            return allocation;
        } else {
            free(sectorBuffer);

            // We can just take the whole cake no need to modify it's length 
            if (firstFreeSize == 0)  {
                firstFreeSize = sizeOfBucket;
            }

            bucketsLeft -= sizeOfBucket;

            if (bucketsLeft != 0 && bucketLink == MFS_ENDOFCHAIN) {
                VLOG_FATAL("mfs-bucket-map", "mfs_bucket_map_allocate: out of sectors, partition is full.");
            }
        }
    }

    // Update BucketPrevPtr to MFS_ENDOFCHAIN
    if (bucketLinkPrevious != MFS_ENDOFCHAIN) {
        uint32_t  __sectorOffset = bucketLinkPrevious / mapEntriesPerSector;
        uint32_t  __sectorIndex = bucketLinkPrevious % mapEntriesPerSector;
        uint32_t* __sectorBuffer = __read_sector(map, __sectorOffset, 1);

        // Modify link
        __sectorBuffer[__sectorIndex * 2] = MFS_ENDOFCHAIN;
        if (map->ops->write(map->map_sector + __sectorOffset, (uint8_t*)__sectorBuffer, 1, map->ops->op_context)) {
            VLOG_FATAL("mfs-bucket-map", "mfs_bucket_map_allocate: failed to write map sector\n");
        }
        free(__sectorBuffer);
    }

    map->next_free_bucket = bucketLink;
    *sizeOfFirstBucket = firstFreeSize;
    return allocation;
}

uint32_t mfs_bucket_map_bucket_info(struct mfs_bucket_map* map, uint32_t bucket, uint32_t* length)
{
    // Calculate index into bucket map
    uint32_t mapEntriesPerSector = map->bytes_per_sector / 8;
    uint32_t sectorOffset = bucket / mapEntriesPerSector;
    uint32_t sectorIndex = bucket % mapEntriesPerSector;
    uint32_t link;

    // Read the calculated sector
    uint32_t* sector = __read_sector(map, sectorOffset, 1);

    // Update length and return link
    link = sector[sectorIndex*2];
    *length = sector[(sectorIndex*2) + 1];
    free(sector);
    return link;
}

void mfs_bucket_map_set_bucket_link(struct mfs_bucket_map* map, uint32_t bucket, uint32_t nextBucket)
{
    // Calculate index into bucket map
    uint32_t mapEntriesPerSector = map->bytes_per_sector / 8;
    uint32_t sectorOffset = bucket / mapEntriesPerSector;
    uint32_t sectorIndex = bucket % mapEntriesPerSector;

    // Read the calculated sector
    uint32_t* sectorBuffer = __read_sector(map, sectorOffset, 1);

    // Update link
    sectorBuffer[sectorIndex * 2] = nextBucket;

    // Flush buffer to disk
    if (map->ops->write(map->map_sector + sectorOffset, (uint8_t*)sectorBuffer, 1, map->ops->op_context)) {
        VLOG_FATAL("mfs-bucket-map", "mfs_bucket_map_set_bucket_link: failed to write map sector\n");
    }
    free(sectorBuffer);
}
