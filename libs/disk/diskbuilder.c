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

#include "mbr.h"
#include "gpt.h"

#define __KB 1024
#define __MB (__KB * 1024)

#define __MIN(a, b) ((a) < (b) ? (a) : (b))

// import assets
extern const unsigned char* g_mbrSector;
extern const unsigned char* g_mbrGptSector;

struct chef_disk_geometry {
    uint64_t  sector_count;
    uint64_t  cylinders;
    uint8_t   sectors_per_track;
    uint8_t   heads_per_cylinder;
    uint16_t  bytes_per_sector;
};

struct chef_disk_partition {
    const char*                    name;
    const char*                    guid;
    uint8_t                        mbr_type;
    uint64_t                       sector_start;
    uint64_t                       sector_count;
    enum chef_partition_attributes attributes;
};

struct chef_diskbuilder {
    enum chef_diskbuilder_schema schema;
    uint64_t                     size;
    FILE*                        image_stream;
    struct chef_disk_geometry    disk_geometry;
    struct list                  partitions; // list<chef_disk_partition>
};

static void __calculate_geometry(struct chef_disk_geometry* geo, uint64_t size, uint16_t sectorSize)
{
    uint8_t heads;

    if (size <= 504 * __MB) {
        heads = 16;
    } else if (size <= (1008 * __MB)) {
        heads = 32;
    } else if (size <= (2016 * __MB)) {
        heads = 64;
    } else if (size <= (4032 * __MB)) {
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

struct chef_diskbuilder* chef_diskbuilder_new(struct chef_diskbuilder_params* params)
{
    struct chef_diskbuilder* builder;

    builder = calloc(1, sizeof(struct chef_diskbuilder));
    if (builder == NULL) {
        return NULL;
    }

    builder->schema = params->schema;
    builder->size = params->size;

    builder->image_stream = fopen(params->path, "wb");
    if (builder->image_stream == NULL) {
        free(builder);
        return NULL;
    }
    __calculate_geometry(&builder->disk_geometry, params->size, params->sector_size);
    return builder;
}

static uint32_t __sector_count_gpt_partition_table(struct chef_disk_geometry* geo)
{
    return (uint32_t)(16384 / geo->bytes_per_sector);
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
    struct list_item*   i;
    struct __gpt_header header;
    struct __gpt_entry* table;
    uint32_t            sectorsForTable;
    int                 ti;

    sectorsForTable = __sector_count_gpt_partition_table(&builder->disk_geometry);
    table = calloc(1, 
        builder->disk_geometry.bytes_per_sector * sectorsForTable
    );
    if (table == NULL) {
        return -1;
    }

    memcpy(&header.signature[0], __GPT_SIGNATURE, 8);
    header.revision = __GPT_REVISION;
    header.header_size = __GPT_HEADER_SIZE;
    header.header_crc32 = 0; // TODO: calculate crc
    header.reserved = 0;

    header.main_lba = 1;
    header.first_usable_lba = 2 + sectorsForTable;
    header.last_usable_lba = builder->disk_geometry.sector_count - (2 + sectorsForTable);
    header.backup_lba = builder->disk_geometry.sector_count - (1 + sectorsForTable);
    header.partition_entry_lba = 2;
    header.partition_entry_count = builder->partitions.count;
    header.partition_entry_size = __GPT_ENTRY_SIZE;
    header.partition_array_crc32 = 0; // TODO: calculate crc
    platform_guid_new(header.disk_guid);

    ti = 0;
    list_foreach(&builder->partitions, i) {
        struct chef_disk_partition* p = (struct chef_disk_partition*)i;
        struct __gpt_entry*         e = &table[ti++];

        if (__convert_utf8_to_utf16(p->name, &e->name_utf16[0])) {
            return -1;
        }

        e->first_sector = p->sector_start;
        e->last_sector = p->sector_start + p->sector_count - 1;
        e->attributes = __gpt_attributes(p);
        platform_guid_new(e->unique_guid);
        platform_guid_parse(e->type_guid, p->guid);
    }
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

static int __write_mbr(struct chef_diskbuilder* builder, const unsigned char* template)
{
    struct list_item* i;
    int               status;
    uint8_t*          mbr;
    int               pi;

    mbr = __memdup(template, 512);
    if (mbr == NULL) {
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

    status = fwrite(mbr, 512, 1, builder->image_stream);
    if (status != 512) {
        return -1;
    }
    return 0;
}

static int __write_bootloader(struct chef_diskbuilder* builder)
{
    switch (builder->schema) {
        case CHEF_DISK_SCHEMA_MBR:
            return __write_mbr(builder, g_mbrSector);
        case CHEF_DISK_SCHEMA_GPT:
            int status = __write_mbr(builder, g_mbrSector);
            if (status) {
                return status;
            }
            return __write_gpt_tables(builder);
        default:
            return -1;
    }
    return 0;
}

int chef_diskbuilder_finish(struct chef_diskbuilder* builder)
{
    int status;



    // write bootsector
    status = __write_bootloader(builder);
    if (status) {
        return status;
    }

    // write partitions


    return 0;
}

struct chef_disk_partition* chef_diskbuilder_partition_new(struct chef_diskbuilder* builder, struct chef_disk_partition_params* params)
{

}
