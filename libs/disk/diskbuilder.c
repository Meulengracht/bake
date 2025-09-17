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

#include <chef/diskbuilder.h>
#include <chef/platform.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <uchar.h>
#include <vlog.h>

#include "private.h"
#include "mbr.h"
#include "gpt.h"

// import assets
extern const unsigned char g_mbrSector[512];
extern const unsigned char g_mbrGptSector[512];

struct chef_disk_geometry {
    uint64_t  sector_count;
    uint64_t  cylinders;
    uint8_t   sectors_per_track;
    uint8_t   heads_per_cylinder;
    uint16_t  bytes_per_sector;
};

struct chef_diskbuilder {
    enum chef_diskbuilder_schema schema;
    uint64_t                     size;
    FILE*                        image_stream;
    struct chef_disk_geometry    disk_geometry;
    struct list                  partitions; // list<chef_disk_partition>

    uint64_t                     next_usable_sector;
    uint64_t                     last_usable_sector;
};

static unsigned int __crc32b(const unsigned char* data, size_t length) {
   int          i, j;
   unsigned int byte, crc, mask;

   i = 0;
   crc = 0xFFFFFFFF;
   while (i < length) {
      byte = data[i];

      crc = crc ^ byte;
      for (j = 7; j >= 0; j--) {    // Do eight times.
         mask = -(crc & 1);
         crc = (crc >> 1) ^ (0xEDB88320 & mask);
      }
      i = i + 1;
   }
   return ~crc;
}

static void __calculate_geometry(struct chef_disk_geometry* geo, uint64_t size, uint16_t sectorSize)
{
    uint8_t heads;
    VLOG_DEBUG("disk", "__calculate_geometry(size=%llu)\n", size);

    if (size <= 504 * __MB) {
        heads = 16;
    } else if (size <= (1008 * __MB)) {
        heads = 32;
    } else if (size <= (2016 * __MB)) {
        heads = 64;
    } else if (size <= (4032ULL * __MB)) {
        heads = 128;
    } else {
        heads = 255;
    }

    geo->sector_count = size / sectorSize;
    geo->bytes_per_sector = sectorSize;
    geo->heads_per_cylinder = heads;
    geo->sectors_per_track = 63U;
    geo->cylinders = __MIN(1024U, size / (63U * (uint64_t)heads * sectorSize));
}

static uint32_t __sector_count_gpt_partition_table(struct chef_disk_geometry* geo)
{
    return (uint32_t)(16384 / geo->bytes_per_sector);
}

static void __set_usable_sectors(struct chef_diskbuilder* builder)
{
    switch (builder->schema) {
        case CHEF_DISK_SCHEMA_MBR:
            builder->next_usable_sector = 1;
            builder->last_usable_sector = builder->disk_geometry.sector_count - 1;
            break;
        case CHEF_DISK_SCHEMA_GPT:
            uint64_t tableSize = __sector_count_gpt_partition_table(&builder->disk_geometry);
            builder->next_usable_sector = 2 + tableSize;
            builder->last_usable_sector = builder->disk_geometry.sector_count - (2 + tableSize);
            break;
        default:
            break;
    }
}

struct chef_diskbuilder* chef_diskbuilder_new(struct chef_diskbuilder_params* params)
{
    struct chef_diskbuilder* builder;
    VLOG_DEBUG("disk", "chef_diskbuilder_new(path=%s, size=%llu)\n", params->path, params->size);

    builder = calloc(1, sizeof(struct chef_diskbuilder));
    if (builder == NULL) {
        VLOG_ERROR("disk", "chef_diskbuilder_new: failed to allocate memory\n");
        return NULL;
    }

    builder->schema = params->schema;
    builder->size = params->size;

    builder->image_stream = fopen(params->path, "wb");
    if (builder->image_stream == NULL) {
        VLOG_ERROR("disk", "chef_diskbuilder_new: failed to open %s\n", params->path);
        free(builder);
        return NULL;
    }
    __calculate_geometry(&builder->disk_geometry, params->size, params->sector_size);
    __set_usable_sectors(builder);
    return builder;
}

static uint64_t __gpt_attributes(struct chef_disk_partition* p)
{
    uint64_t flags = 0;

    if (p->attributes & CHEF_PARTITION_ATTRIBUTE_BOOT) {
        flags |= __GPT_ENTRY_ATTRIB_LEGACY_BIOS_BOOTABLE;
    }

    if (p->attributes & CHEF_PARTITION_ATTRIBUTE_READONLY) {
        flags |= __GPT_ENTRY_ATTRIB_READONLY;
    }

    if (p->attributes & CHEF_PARTITION_ATTRIBUTE_NOAUTOMOUNT) {
        flags |= __GPT_ENTRY_ATTRIB_NO_AUTOMOUNT;
    }

    return flags;
}

static int __convert_utf8_to_utf16(const char* utf8, uint16_t* utf16)
{
    size_t ni = 0;
    size_t nlimit = strlen(utf8);
    int    ei = 0;
    while (ni < nlimit) {
        mbstate_t ps = { 0 };
        int       length;

        length = (int)mbrtoc16(&utf16[ei++], &utf8[ni], MB_CUR_MAX, &ps);
        if (length < 0) {
            return -1;
        }
        ni += length;
    }
    return 0;
}

static int __write_gpt_tables(struct chef_diskbuilder* builder)
{
    struct list_item*    i;
    void*                headerSector;
    struct __gpt_header* header;
    struct __gpt_entry*  table;
    uint32_t             sectorsForTable;
    int                  ti;
    int                  status = 0;
    size_t               written;
    VLOG_DEBUG("disk", "__write_gpt_tables()\n");

    headerSector = calloc(1, builder->disk_geometry.bytes_per_sector);
    if (headerSector == NULL) {
        VLOG_ERROR("disk", "__write_gpt_tables: failed to allocate gpt header\n");
        return -1;
    }
    header = headerSector;

    sectorsForTable = __sector_count_gpt_partition_table(&builder->disk_geometry);
    table = calloc(1, 
        builder->disk_geometry.bytes_per_sector * sectorsForTable
    );
    if (table == NULL) {
        VLOG_ERROR("disk", "__write_gpt_tables: failed to allocate gpt table\n");
        free(table);
        return -1;
    }

    memcpy(&header->signature[0], __GPT_SIGNATURE, 8);
    header->revision = __GPT_REVISION;
    header->header_size = __GPT_HEADER_SIZE;
    header->header_crc32 = 0; // this must be zero during calculation
    header->reserved = 0;

    header->main_lba = 1;
    header->first_usable_lba = 2 + sectorsForTable;
    header->last_usable_lba = builder->disk_geometry.sector_count - (2 + sectorsForTable);
    header->backup_lba = builder->disk_geometry.sector_count - (1 + sectorsForTable);
    header->partition_entry_lba = 2;
    header->partition_entry_count = builder->partitions.count;
    header->partition_entry_size = __GPT_ENTRY_SIZE;
    platform_guid_new(header->disk_guid);

    ti = 0;
    list_foreach(&builder->partitions, i) {
        struct chef_disk_partition* p = (struct chef_disk_partition*)i;
        struct __gpt_entry*         e = &table[ti++];

        if (__convert_utf8_to_utf16(p->name, &e->name_utf16[0])) {
            VLOG_ERROR("disk", "__write_gpt_tables: failed to convert partition name %s\n", p->name);
            status = -1;
            goto cleanup;
        }

        e->first_sector = p->sector_start;
        e->last_sector = p->sector_start + p->sector_count - 1;
        e->attributes = __gpt_attributes(p);
        platform_guid_new(e->unique_guid);
        platform_guid_parse(e->type_guid, p->guid);
    }

    // calculate CRC, first the array member
    header->partition_array_crc32 = __crc32b((unsigned char*)table, builder->partitions.count * __GPT_ENTRY_SIZE);
    header->header_crc32 = __crc32b(headerSector, __GPT_HEADER_SIZE);

    // just write the main header and the table
    written = fwrite(&header, builder->disk_geometry.bytes_per_sector, 1, builder->image_stream);
    if (written != builder->disk_geometry.bytes_per_sector) {
        VLOG_ERROR("disk", "__write_gpt_tables: failed write primary gpt header\n");
        status = -1;
        goto cleanup;
    }

    written = fwrite(table, 
        builder->disk_geometry.bytes_per_sector,
        sectorsForTable,
        builder->image_stream
    );
    if (written != (builder->disk_geometry.bytes_per_sector * sectorsForTable)) {
        VLOG_ERROR("disk", "__write_gpt_tables: failed write primary gpt table\n");
        status = -1;
        goto cleanup;
    }

    // prepare backup header
    header->main_lba = builder->disk_geometry.sector_count - (1 + sectorsForTable);
    header->partition_entry_lba = builder->disk_geometry.sector_count - sectorsForTable;
    header->backup_lba = 1;
    header->header_crc32 = 0;
    header->header_crc32 = __crc32b(headerSector, __GPT_HEADER_SIZE);

    // seek to the correct space in the file
    fseek(
        builder->image_stream, 
        builder->disk_geometry.bytes_per_sector * builder->disk_geometry.sector_count - (1 + sectorsForTable),
        SEEK_SET
    );

    // write backup
    written = fwrite(&header, builder->disk_geometry.bytes_per_sector, 1, builder->image_stream);
    if (written != builder->disk_geometry.bytes_per_sector) {
        VLOG_ERROR("disk", "__write_gpt_tables: failed write secondary gpt header\n");
        status = -1;
        goto cleanup;
    }

    written = fwrite(table, 
        builder->disk_geometry.bytes_per_sector,
        sectorsForTable,
        builder->image_stream
    );
    if (written != (builder->disk_geometry.bytes_per_sector * sectorsForTable)) {
        VLOG_ERROR("disk", "__write_gpt_tables: failed write secondary gpt table\n");
        status = -1;
    }

cleanup:
    free(headerSector);
    free(table);
    return 0;
}

static void* __memdup(const void* data, size_t size)
{
    void* copy = malloc(size);
    if (copy == NULL) {
        return NULL;
    }
    memcpy(copy, data, size);
    return copy;
}

static int __write_mbr(struct chef_diskbuilder* builder, const unsigned char* template, size_t size)
{
    struct list_item* i;
    size_t            written;
    uint8_t*          mbr;
    int               pi;
    VLOG_DEBUG("disk", "__write_mbr()\n");

    mbr = __memdup(template, size);
    if (mbr == NULL) {
        VLOG_ERROR("disk", "__write_mbr: failed to allocate memory for mbr\n");
        return -1;
    }

    pi = 0;
    list_foreach(&builder->partitions, i) {
        struct chef_disk_partition* p = (struct chef_disk_partition*)i;
        int                         offset = __MBR_PARTITION(pi++);
        int                         pstatus;
        uint8_t                     sectorsPerTrack;
        uint8_t                     headOfStart, headOfEnd;
        uint16_t                    cylinderOfStart, cylinderOfEnd;
        uint8_t                     sectorInCylinderStart, sectorInCylinderEnd;

        if (p->attributes & CHEF_PARTITION_ATTRIBUTE_BOOT) {
            pstatus |= 0x80;
        }

        sectorsPerTrack = builder->disk_geometry.sectors_per_track;

        headOfStart = (uint8_t)((p->sector_start / sectorsPerTrack) % 16);
        headOfEnd = (uint8_t)(((p->sector_start + p->sector_count) / sectorsPerTrack) % 16);

        cylinderOfStart = __MIN((uint16_t)(p->sector_start / (sectorsPerTrack * 16)), (uint16_t)1023);
        cylinderOfEnd = __MIN((uint16_t)((p->sector_start + p->sector_count) / (sectorsPerTrack * 16)), (uint16_t)1023);

        sectorInCylinderStart = (uint8_t)((p->sector_start % sectorsPerTrack) + 1);
        sectorInCylinderEnd = (uint8_t)(((p->sector_start + p->sector_count) % sectorsPerTrack) + 1);

        // Set partition status
        mbr[offset] = pstatus;

        // Set partiton start (CHS), high byte is low byte of cylinder
        mbr[offset + 1] = (uint8_t)headOfStart;
        mbr[offset + 2] = (uint8_t)((uint8_t)((cylinderOfStart >> 2) & 0xC0) | (uint8_t)(sectorInCylinderStart & 0x3F));
        mbr[offset + 3] = (uint8_t)(cylinderOfStart & 0xFF);

        // Set partition type
        mbr[offset + 4] = p->mbr_type;

        // Set partition end (CHS), high byte is low byte of cylinder
        mbr[offset + 5] = (uint8_t)headOfEnd;
        mbr[offset + 6] = (uint8_t)((uint8_t)((cylinderOfEnd >> 2) & 0xC0) | (uint8_t)(sectorInCylinderEnd & 0x3F));
        mbr[offset + 7] = (uint8_t)(cylinderOfEnd & 0xFF);

        // Set partition start (LBA)
        mbr[offset + 8] = (uint8_t)(p->sector_start & 0xFF);
        mbr[offset + 9] = (uint8_t)((p->sector_start >> 8) & 0xFF);
        mbr[offset + 10] = (uint8_t)((p->sector_start >> 16) & 0xFF);
        mbr[offset + 11] = (uint8_t)((p->sector_start >> 24) & 0xFF);

        // Set partition size (LBA)
        mbr[offset + 12] = (uint8_t)(p->sector_count & 0xFF);
        mbr[offset + 13] = (uint8_t)((p->sector_count >> 8) & 0xFF);
        mbr[offset + 14] = (uint8_t)((p->sector_count >> 16) & 0xFF);
        mbr[offset + 15] = (uint8_t)((p->sector_count >> 24) & 0xFF);
    }

    written = fwrite(mbr, 1, size, builder->image_stream);
    free(mbr);
    if (written != size) {
        VLOG_ERROR("disk", "__write_mbr: failed to write the mbr sector (%zu != %zu)\n", written, size);
        return -1;
    }
    return 0;
}

static int __write_bootloader(struct chef_diskbuilder* builder)
{
    VLOG_DEBUG("disk", "__write_bootloader()\n");
    switch (builder->schema) {
        case CHEF_DISK_SCHEMA_MBR:
            return __write_mbr(builder, &g_mbrSector[0], 512);
        case CHEF_DISK_SCHEMA_GPT:
            int status = __write_mbr(builder, &g_mbrGptSector[0], 512);
            if (status) {
                VLOG_ERROR("disk", "__write_bootloader: failed to write mbr\n");
                return status;
            }
            return __write_gpt_tables(builder);
        default:
            VLOG_ERROR("disk", "__write_bootloader: unknown disk schema\n");
            return -1;
    }
    return 0;
}

static int __write_partition(struct chef_disk_partition* p, unsigned int sectorSize, FILE* stream)
{
    void*  block;
    size_t read;
    VLOG_DEBUG("disk", "__write_partition(name=%s)\n", p->name);

    // seek to first sector
    fseek(stream, p->sector_start * sectorSize, SEEK_SET);
    fseek(p->stream, 0, SEEK_SET);

    // allocate a 4mb block for transfers
    block = malloc(4 * __MB);
    if (block == NULL) {
        VLOG_ERROR("disk", "__write_partition: failed to allocate transfer buffer\n");
        return -1;
    }

    for (;;) {
        size_t written;

        read = fread(block, 1, 4 * __MB, p->stream);
        if (read == 0) {
            break;
        }

        written = fwrite(block, 1, read, stream);
        if (written != read) {
            VLOG_ERROR("disk", "__write_partition: failed to write partition data\n");
            free(block);
            return -1;
        }
    }

    // do not need this anymore
    free(block);

    fclose(p->stream);
    p->stream == NULL;
    return 0;
}

int chef_diskbuilder_finish(struct chef_diskbuilder* builder)
{
    struct list_item* i;
    int               status;
    VLOG_DEBUG("disk", "chef_diskbuilder_finish()\n");

    if (builder->image_stream == NULL) {
        VLOG_ERROR("disk", "chef_diskbuilder_finish: builder already finished\n");
        return -1;
    }

    status = __write_bootloader(builder);
    if (status) {
        VLOG_ERROR("disk", "chef_diskbuilder_finish: failed to write boot sectors\n");
        return status;
    }

    list_foreach(&builder->partitions, i) {
        struct chef_disk_partition* p = (struct chef_disk_partition*)i;
        status = __write_partition(p, builder->disk_geometry.bytes_per_sector, builder->image_stream);
        if (status) {
            VLOG_ERROR("disk", "chef_diskbuilder_finish: failed to write partition %s\n", p->name);
            return status;
        }
    }

    fflush(builder->image_stream);
    fclose(builder->image_stream);
    builder->image_stream = NULL;
    return 0;
}

static void __partition_delete(struct chef_disk_partition* partition)
{
    if (partition == NULL) {
        return;
    }

    free((void*)partition->name);
    free((void*)partition->guid);
    free(partition);
}

void chef_diskbuilder_delete(struct chef_diskbuilder* builder)
{
    if (builder == NULL) {
        return;
    }

    list_destroy(&builder->partitions, (void(*)(void*))__partition_delete);
    free(builder);
}

struct chef_disk_partition* chef_diskbuilder_partition_new(struct chef_diskbuilder* builder, struct chef_disk_partition_params* params)
{
    struct chef_disk_partition* p;
    int    status;
    char   tmp[PATH_MAX];
    VLOG_DEBUG("disk", "chef_diskbuilder_partition_new(name=%s)\n", params->name);

    // Is the builder done already?
    if (builder->image_stream == NULL) {
        VLOG_ERROR("disk", "chef_diskbuilder_partition_new: builder already finished\n");
        return NULL;
    }

    // Make sure the size fits
    if (params->size > (builder->last_usable_sector - builder->next_usable_sector)) {
        VLOG_ERROR("disk", 
            "chef_diskbuilder_partition_new: partition %s: size does not fit onto image\n",
            params->name
        );
        return NULL;
    }

    // Make sure there are actually sectors left, i.e no double zero partitions
    if (builder->last_usable_sector - builder->next_usable_sector == 0) {
        VLOG_ERROR("disk", 
            "chef_diskbuilder_partition_new: partition %s: no sectors left\n",
            params->name
        );
        return NULL;
    }

    p = calloc(1, sizeof(struct chef_disk_partition));
    if (p == NULL) {
        VLOG_ERROR("disk", "chef_diskbuilder_partition_new: failed to allocate memory\n");
        return NULL;
    }

    // If size is not specified, it's meant to take up the rest of the disk space, and
    // this can obviously only be done for the final partition.
    p->sector_start = builder->next_usable_sector;
    if (params->size) {
        p->sector_count = params->size / builder->disk_geometry.bytes_per_sector;
    } else {
        p->sector_count = builder->last_usable_sector - builder->next_usable_sector;
    }

    snprintf(
        &tmp[0],
        sizeof(tmp),
        "%s" CHEF_PATH_SEPARATOR_S "%s-stream",
        params->work_directory, params->name
    );

    p->stream = fopen(&tmp[0], "w+b");
    if (p->stream == NULL) {
        VLOG_ERROR("disk", "chef_diskbuilder_partition_new: failed to open stream %s\n", &tmp[0]);
        free((void*)p->guid);
        free(p);
        return NULL;
    }

    // make the stream unbuffered, we need changes written immediately
    setvbuf(p->stream, NULL, _IONBF, 0);

    VLOG_DEBUG("disk", "chef_diskbuilder_partition_new: resizing stream to %llu\n", p->sector_count * builder->disk_geometry.bytes_per_sector);
    status = platform_chsize(fileno(p->stream), (long)(p->sector_count * builder->disk_geometry.bytes_per_sector));
    if (status) {
        VLOG_ERROR("disk", "chef_diskbuilder_partition_new: failed to resize stream %s\n", &tmp[0]);
        fclose(p->stream);
        free((void*)p->guid);
        free(p);
        return NULL;
    }

    p->name = platform_strdup(params->name);
    // GUID is not always required
    if (params->guid != NULL) {
        p->guid = platform_strdup(params->guid);
    }
    p->attributes = params->attributes;
    p->mbr_type = params->type;

    // increase the next usable sector
    builder->next_usable_sector += p->sector_count;
    list_add(&builder->partitions, &p->list_header);
    return p;
}

int chef_diskbuilder_partition_finish(struct chef_disk_partition* partition)
{
    VLOG_DEBUG("disk", "chef_diskbuilder_partition_finish()\n");
    if (partition->stream == NULL) {
        VLOG_ERROR("disk", "chef_diskbuilder_partition_new: partition already finished\n");
        return -1;
    }

    fflush(partition->stream);
    return 0;
}
