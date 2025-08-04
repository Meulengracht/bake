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

#include "api.h"
#include "private.h"

#define __MIN(a, b) (((a) < (b)) ? (a) : (b))
#define __BUCKET_SECTOR(b) ((mfs->reserved_sector_count) + (uint64_t)(b * mfs->bucket_size))

static uint8_t* __new_buffer(struct mfs* mfs, uint32_t sectorCount)
{
    uint8_t* buffer;

    buffer = calloc(sectorCount, mfs->bytes_per_sector);
    if (buffer == NULL) {
        VLOG_FATAL("mfs", "__new_buffer: could not allocate memory for sectors\n");
    }
    return buffer;
}

static int __fill_bucket_chain(struct mfs* mfs, uint32_t bucket, uint32_t bucketLength, const uint8_t* data, size_t length)
{
    uint32_t bucketLengthItr = bucketLength;
    uint32_t bucketPtr = bucket;
    uint64_t index = 0;
    int      status;

    // Iterate through the data and write it to the buckets
    while (index < length) {
        uint8_t* buffer = __new_buffer(mfs, mfs->bucket_size * bucketLengthItr);
        size_t   bufferLength = mfs->bucket_size * bucketLengthItr * mfs->bytes_per_sector;

        memcpy(buffer, &data[index], __MIN(bufferLength, length - index));
        index += bufferLength;

        status = mfs->ops.write(__BUCKET_SECTOR(bucketPtr), buffer, mfs->bucket_size * bucketLengthItr, mfs->ops.op_context);
        free(buffer);
        if (status) {
            VLOG_ERROR("mfs", "__fill_bucket_chain: failed to write bucket data\n");
            return status;
        }

        // Get next bucket cluster for writing
        bucketPtr = mfs_bucket_map_bucket_info(mfs->map, bucketPtr, &bucketLengthItr);
        if (bucketPtr == MFS_ENDOFCHAIN) {
            break;
        }

        // Get length of new bucket
        (void)mfs_bucket_map_bucket_info(mfs->map, bucketPtr, &bucketLengthItr);
    }
}

static uint8_t* __read_sector(struct mfs* mfs, uint64_t sector, uint32_t count)
{
    uint8_t* buffer;
    int      status;

    buffer = malloc(count * mfs->bytes_per_sector);
    if (buffer == NULL) {
        VLOG_FATAL("mfs-bucket-map", "__read_sector: failed to allocate memory for buffer\n");
    }

    status = mfs->ops.read(sector, (uint8_t*)buffer, count, mfs->ops.op_context);
    if (status) {
        VLOG_FATAL("mfs-bucket-map", "__read_sector: failed to read sector\n");
    }
    return buffer;
}

static int __save_next_available_bucket(struct mfs* mfs)
{
    uint8_t* bootSector;
    uint8_t* masterRecord;
    uint64_t masterRecordSector, masterRecordMirrorSector;
    uint32_t nextFree;
    int      status;

    // Get relevant locations
    bootSector = __read_sector(mfs, 0, 1);
    masterRecordSector = *((uint64_t*)&bootSector[28]);
    masterRecordMirrorSector = *((uint64_t*)&bootSector[36]);
    
    // Update the master-record to reflect the new index
    masterRecord = __read_sector(mfs, masterRecordSector, 1);
    nextFree = mfs_bucket_map_next_free(mfs->map);
    masterRecord[76] = (uint8_t)(nextFree & 0xFF);
    masterRecord[77] = (uint8_t)((nextFree >> 8) & 0xFF);
    masterRecord[78] = (uint8_t)((nextFree >> 16) & 0xFF);
    masterRecord[79] = (uint8_t)((nextFree >> 24) & 0xFF);

    status = mfs->ops.write(masterRecordSector, masterRecord, 1, mfs->ops.op_context);
    if (status) {
        VLOG_ERROR("mfs", "__save_next_available_bucket: failed to write primary record\n");
        free(masterRecord);
        return status;
    }

    status = mfs->ops.write(masterRecordMirrorSector, masterRecord, 1, mfs->ops.op_context);
    if (status) {
        VLOG_ERROR("mfs", "__save_next_available_bucket: failed to write secondary record\n");
        free(masterRecord);
        return status;
    }
    free(masterRecord);
    return 0;
}

static uint32_t __get_last_bucket(struct mfs* mfs, uint32_t start)
{
    uint32_t bucketPtr = start;
    uint32_t bucketPrevPtr = MFS_ENDOFCHAIN;
    uint32_t bucketLength = 0;
    while (bucketPtr != MFS_ENDOFCHAIN) {
        bucketPrevPtr = bucketPtr;
        bucketPtr = mfs_bucket_map_bucket_info(mfs->map, bucketPtr, &bucketLength);
    }
    return bucketPrevPtr;
}

static int __ensure_bucket_space(struct mfs* mfs, struct mfs_record* record, uint64_t size)
{
    uint32_t initialBucketSize = 0;
    uint32_t bucketAllocation;
    uint64_t sectorCount;
    uint32_t bucketCount;
    uint32_t finalBucket;

    if (size < record->allocated_size) {
        return 0;
    }

    // calculate only the difference in allocation size
    sectorCount = (size - record->allocated_size) / mfs->bytes_per_sector;
    if (((size - record->allocated_size) % mfs->bytes_per_sector) > 0)
        sectorCount++;
    bucketCount = (uint32_t)(sectorCount / mfs->bucket_size);
    if ((sectorCount % mfs->bucket_size) > 0)
        bucketCount++;

    VLOG_DEBUG("mfs", "__ensure_bucket_space: allocating %u buckets\n", bucketCount);
   
    bucketAllocation = mfs_bucket_map_allocate(mfs->map, bucketCount, &initialBucketSize);
    VLOG_DEBUG("mfs", "__ensure_bucket_space: allocated bucket %u\n", bucketAllocation);

    // Iterate to end of data chain, but keep a pointer to the previous
    finalBucket = __get_last_bucket(mfs, record->bucket);

    // Update the last link to the newly allocated, we only do this if 
    // the previous one was not end of chain (none allocated for record)
    if (finalBucket != MFS_ENDOFCHAIN) {
        mfs_bucket_map_set_bucket_link(mfs->map, finalBucket, bucketAllocation);
    }

    // Update the allocated size in cached
    record->allocated_size += (bucketCount * mfs->bucket_size * mfs->bytes_per_sector);

    // Initiate the bucket in the record if it was new
    if (record->bucket == MFS_ENDOFCHAIN) {
        record->bucket = bucketAllocation;
        record->bucket_length = bucketCount;
    }
    return 0;
}

static char* __safe_path(const char* path)
{
    return strreplace(path, "\\", "/");
}

static int __is_record_used(struct mfs_record* record)
{
    return record->flags & MFS_RECORD_FLAG_INUSE;
}

static int __record_name_length(const uint8_t* buffer, int offset)
{
    int length = 0;
    while (buffer[offset + 68 + length] != 0) {
        length++;
    }
    return length;
}

static char* __record_name(const uint8_t* buffer, int offset)
{
    int length = __record_name_length(buffer, offset);
    return platform_strdup(&buffer[offset + 64]);
}

static struct mfs_record* __parse_record(const uint8_t* buffer, int offset, uint32_t directoryBucket, uint32_t directoryBucketLength)
{
    struct mfs_record* record;

    record = calloc(1, sizeof(struct mfs_record));
    if (record == NULL) {
        return NULL;
    }

    record->name = __record_name(buffer, offset);
    record->flags = *((uint32_t*)&buffer[offset]);
    record->bucket = *((uint32_t*)&buffer[offset + 4]);
    record->bucket_length = *((uint32_t*)&buffer[offset + 8]);
    record->size = *((uint64_t*)&buffer[offset + 48]);
    record->allocated_size = *((uint64_t*)&buffer[offset + 56]);

    record->directory_bucket = directoryBucket;
    record->directory_length = directoryBucketLength;
    record->directory_index = (uint32_t)(offset / MFS_RECORDSIZE);
    return record;
}

static void __write_record(uint8_t* buffer, int offset, struct mfs_record* record)
{
    // copy name + terminating null
    memcpy(&buffer[offset + 68], record->name, strlen(record->name) + 1);

    *((uint32_t*)&buffer[offset]) = record->flags;
    *((uint32_t*)&buffer[offset + 4]) = record->bucket;
    *((uint32_t*)&buffer[offset + 8]) = record->bucket_length;
    *((uint64_t*)&buffer[offset + 48]) = record->size;
    *((uint64_t*)&buffer[offset + 56]) = record->allocated_size;
}

static struct mfs_record* __find_record(struct mfs* mfs, uint32_t directoryBucket, const char* name)
{
    uint32_t bucketLength  = 0;
    uint32_t currentBucket = directoryBucket;
    for (;;) {
        uint32_t bucketLink   = mfs_bucket_map_bucket_info(mfs->map, currentBucket, &bucketLength);
        uint8_t* bucketBuffer = __read_sector(mfs, __BUCKET_SECTOR(currentBucket), mfs->bucket_size * bucketLength);
        
        uint32_t bytesToIterate = mfs->bucket_size * mfs->bytes_per_sector * bucketLength;
        for (uint32_t i = 0; i < bytesToIterate; i += MFS_RECORDSIZE) {
            struct mfs_record* record = __parse_record(bucketBuffer, i, currentBucket, bucketLength);
            if (!__is_record_used(record)) {
                continue;
            }
            if (strcmp(record->name, name) == 0) {
                free(bucketBuffer);
                return record;
            }
        }
        free(bucketBuffer);

        if (bucketLink == MFS_ENDOFCHAIN) {
            break;
        }
        currentBucket = bucketLink;
    }
    return NULL;
}

static void __initiate_directory_record(struct mfs* mfs, struct mfs_record* record)
{
    uint32_t initialBucketSize = 0;
    uint32_t bucket = mfs_bucket_map_allocate(mfs, MFS_EXPANDSIZE, out initialBucketSize);
    uint8_t* buffer;

    // Wipe the new bucket to zeros
    buffer = new byte[mfs->bucket_size * _disk.Geometry.BytesPerSector * initialBucketSize];
    _disk.Write(wipeBuffer, __BUCKET_SECTOR(bucket), true);

    record->bucket = bucket;
    record->bucket_length = initialBucketSize;
}

static uint32_t ExpandDirectory(struct mfs* mfs, uint32_t lastBucket)
{
    uint32_t initialBucketSize = 0;
    uint32_t bucket = _bucketMap.AllocateBuckets(MFS_EXPANDSIZE, out initialBucketSize);
    mfs_bucket_map_set_bucket_link(mfs->map, lastBucket, bucket);
    
    // Wipe the new bucket to zeros
    byte[] wipeBuffer = new byte[mfs->bucket_size * _disk.Geometry.BytesPerSector * initialBucketSize];
    _disk.Write(wipeBuffer, __BUCKET_SECTOR(bucket), true);
    return bucket;
}

static void __update_record(struct mfs* mfs, struct mfs_record* record)
{
    Utils.Logger.Instance.Debug($"UpdateRecord(record={record.Name})");
    Utils.Logger.Instance.Debug($"UpdateRecord reading sector {__BUCKET_SECTOR(record.DirectoryBucket)}, length {mfs->bucket_size * record.DirectoryLength}");
    var bucketBuffer = _disk.Read(__BUCKET_SECTOR(record.DirectoryBucket), mfs->bucket_size * record.DirectoryLength);
    var offset = record.DirectoryIndex * MFS_RECORDSIZE;
    Utils.Logger.Instance.Debug($"UpdateRecord record offset at {offset}");
    WriteRecord(bucketBuffer, (int)offset, record);
    _disk.Write(bucketBuffer, __BUCKET_SECTOR(record.DirectoryBucket), true);
}

        private MfsRecord CreateRecord(uint32_t directoryBucket, string recordName, RecordFlags flags)
        {
            Utils.Logger.Instance.Debug("CreateRecord(" + directoryBucket.ToString() + ", " + recordName + ")");
            uint32_t bucketLength = 0;
            uint32_t currentBucket = directoryBucket;
            while (true)
            {
                Utils.Logger.Instance.Debug($"CreateRecord retrieving link and length of bucket {currentBucket}");
                uint32_t bucketLink = _bucketMap.GetBucketLengthAndLink(currentBucket, out bucketLength);
                Utils.Logger.Instance.Debug($"CreateRecord reading sector {__BUCKET_SECTOR(currentBucket)}, count {mfs->bucket_size * bucketLength}");
                var  bucketBuffer = _disk.Read(__BUCKET_SECTOR(currentBucket), mfs->bucket_size * bucketLength);
                
                var bytesToIterate = mfs->bucket_size * _disk.Geometry.BytesPerSector * bucketLength;
                for (int i = 0; i < bytesToIterate; i += MFS_RECORDSIZE)
                {
                    Utils.Logger.Instance.Debug($"CreateRecord parsing record {i}");
                    var record = __parse_record(bucketBuffer, i, currentBucket, bucketLength);
                    if (__is_record_used(record))
                        continue;
                    
                    Utils.Logger.Instance.Debug($"CreateRecord record {i} was available");
                    record.Name = recordName;
                    record.Flags = flags | RecordFlags.InUse;
                    record.Bucket = MFS_ENDOFCHAIN;
                    record.BucketLength = 0;
                    record.AllocatedSize = 0;
                    record.Size = 0;
                    if (flags.HasFlag(RecordFlags.Directory))
                        InitiateDirectoryRecord(record);
                    UpdateRecord(record);
                    return record;
                }

                if (bucketLink == MFS_ENDOFCHAIN)
                    currentBucket = ExpandDirectory(currentBucket);
                else
                    currentBucket = bucketLink;
            }
        }

        private RecordFlags GetRecordFlags(FileFlags fileFlags)
        {
            RecordFlags recFlags = 0;

            if (fileFlags.HasFlag(FileFlags.Directory)) recFlags |= RecordFlags.Directory;
            if (fileFlags.HasFlag(FileFlags.System))    recFlags |= RecordFlags.System;

            return recFlags;
        }

        private MfsRecord CreatePath(uint32_t directoryBucket, string path, FileFlags fileFlags)
        {
            var safePath = SafePath(path);
            Utils.Logger.Instance.Debug("CreatePath(" + directoryBucket.ToString() + ", " + safePath + ")");

            // split path into tokens
            var tokens = safePath.Split('/');

            uint32_t startBucket = directoryBucket;
            for (int i = 0; i < tokens.Length; i++) {
                var token = tokens[i];
                var isLast = i == tokens.Length - 1;
                var flags = isLast ? GetRecordFlags(fileFlags) : RecordFlags.Directory;
                
                // skip empty tokens
                if (token == "") {
                    continue;
                }
                
                // find the token in the bucket
                var record = FindRecord(startBucket, token);
                if (record == null)
                {
                    record = CreateRecord(startBucket, token, flags);
                    if (record == null)
                    {
                        Utils.Logger.Instance.Error($"Failed to create record {token} in path {safePath}");
                        break;
                    }
                }

                // make sure record is a directory, should be if we just
                // created it tho
                if (!isLast && !record.Flags.HasFlag(RecordFlags.Directory))
                {
                    Utils.Logger.Instance.Error($"Record {token} in path {safePath} is not a directory");
                    break;
                }

                // successful termination condition
                if (isLast)
                    return record;
                
                startBucket = record.Bucket;
            }
            return null;
        }

static struct mfs_record* __create_root_record(void)
{
    return new MfsRecord {
        Name = "<root>",
        Flags = RecordFlags.Directory | RecordFlags.System,
    };
}

static struct mfs_record* __find_path(uint32_t directoryBucket, string path)
{
    struct mfs_record* record = NULL;
    char*              safePath = __safe_path(path);
    uint32_t           startBucket;
    char**             tokens;
    VLOG_DEBUG("mfs", "__find_path(%u, %s)\n", directoryBucket, safePath);

    // If the root path was specified (/ or empty), then we must fake the root
    // record for MFS
    if (safePath == NULL || strlen(safePath) == 0) {
        return __create_root_record();
    }

    // split path into tokens
    tokens = strsplit('/');
    startBucket = directoryBucket;
    for (int i = 0; i < tokens.Length; i++) {
        const char* token = tokens[i];

        // skip empty tokens
        if (strlen(token) == 0) {
            continue;
        }

        // find the token in the bucket
        record = FindRecord(startBucket, token);
        if (record == NULL) {
            break;
        }

        // make sure record is a directory, should be if we just
        // created it tho
        if (!record.Flags.HasFlag(RecordFlags.Directory)) {
            VLOG_ERROR("mfs", "__find_path: record %s in path %s is not a directory\n", token, safePath);
            record = NULL;
            break;
        }
        startBucket = record.Bucket;
    }
    strsplit_free(tokens);
    return record;
}

int mfs_create_file(struct mfs* mfs, const char* path, enum mfs_record_flags flags, uint8_t* data, size_t length)
{
    struct mfs_record* record;

    byte[] bootsector = _disk.Read(_sector, 1);

    // Load some data (master-record and bucket-size)
    uint64_t MasterRecordSector = BitConverter.ToUInt64(bootsector, 28);
    uint64_t MasterRecordMirrorSector = BitConverter.ToUInt64(bootsector, 36);

    // Read master-record
    byte[] masterRecord = _disk.Read(_sector + MasterRecordSector, 1);
    uint32_t rootBucket = BitConverter.ToUInt32(masterRecord, 80);
    uint64_t sectorsRequired = 0;
    uint32_t bucketsRequired = 0;

    if (data != null) {
        // Calculate number of sectors required
        sectorsRequired = (uint64_t)data.LongLength / _disk.Geometry.BytesPerSector;
        if (((uint64_t)data.LongLength % _disk.Geometry.BytesPerSector) > 0)
            sectorsRequired++;

        // Calculate the number of buckets required
        bucketsRequired = (uint)(sectorsRequired / mfs->bucket_size);
        if ((sectorsRequired % mfs->bucket_size) > 0)
            bucketsRequired++;
    }

    // Locate the record
    record = __find_path(rootBucket, localPath);
    if (record == NULL) {
        Utils.Logger.Instance.Debug("/" + localPath + " is a new "
            + (fileFlags.HasFlag(FileFlags.Directory) ? "directory" : "file"));
        record = CreatePath(rootBucket, localPath, fileFlags);
        if (record == null) {
            Utils.Logger.Instance.Error("The creation info returned null, somethings wrong");
            return false;
        }
    }

    if (data != NULL) {
        __ensure_bucket_space(mfs, record, length);
        __fill_bucket_chain(record.Bucket, record.BucketLength, data, length);

        // Update the record
        record->size = (uint64_t)data.LongLength;
        __update_record(mfs, record);
    }
    return true;
}

int mfs_create_directory(struct mfs* mfs, const char* path)
{
    return mfs_create_file(mfs, path, MFS_RECORD_FLAG_DIRECTORY, NULL, 0);
}
