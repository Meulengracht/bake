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
#include <stdlib.h>
#include <stdio.h>

#define __KB 1024
#define __MB (__KB * 1024)

#define __MIN(a, b) ((a) < (b) ? (a) : (b))

// import assets
extern const unsigned char* g_mbrSector;
extern const unsigned char* g_mbrGptSector;

struct chef_disk_geometry {
    unsigned int cylinders;
    unsigned int sectors_per_track;
    unsigned int heads_per_cylinder;
    unsigned int bytes_per_sector;
};

struct chef_diskbuilder {
    enum chef_diskbuilder_schema schema;
    unsigned long long           size;
    FILE*                        image_stream;
    struct chef_disk_geometry    disk_geometry;
};

static void __calculate_geometry(struct chef_disk_geometry* geo, unsigned long long size, unsigned int sectorSize)
{
    unsigned int heads;

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

    geo->bytes_per_sector = sectorSize;
    geo->heads_per_cylinder = heads;
    geo->sectors_per_track = 63U;
    geo->cylinders = __MIN(1024U, size / (63U * (unsigned long long)heads * sectorSize));
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

static int __sector_count_gpt_partition_table(struct chef_disk_geometry* geo)
{
    return (int)(16384 / geo->bytes_per_sector);
}

static int __write_gpt_table(struct chef_diskbuilder* builder)
{

}

static int __write_mbr(struct chef_diskbuilder* builder)
{

}

static int __write_bootloader(struct chef_diskbuilder* builder)
{
    switch (builder->schema) {
        case CHEF_DISK_SCHEMA_MBR:
            int written = fwrite(g_mbrSector, 512, 1, builder->image_stream);
            if (written != 512) {
                return -1;
            }
            break;
        case CHEF_DISK_SCHEMA_GPT:
            int written = fwrite(g_mbrGptSector, 512, 1, builder->image_stream);
            if (written != 512) {
                return -1;
            }
            return __write_gpt_table(builder);
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
