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

#ifndef __DISKBUILDER_H__
#define __DISKBUILDER_H__

#include <chef/list.h>

// transparent types
struct chef_diskbuilder;
struct chef_disk_partition;

enum chef_diskbuilder_schema {
    CHEF_DISK_SCHEMA_MBR,
    CHEF_DISK_SCHEMA_GPT
};

struct chef_diskbuilder_params {
    enum chef_diskbuilder_schema schema;
    unsigned long long           size;
    unsigned int                 sector_size;
    const char*                  path;
};

/**
 * @brief
 */
extern struct chef_diskbuilder* chef_diskbuilder_new(struct chef_diskbuilder_params* params);

/**
 * @brief
 */
extern void chef_diskbuilder_delete(struct chef_diskbuilder* builder);

/**
 * @brief
 */
extern int chef_diskbuilder_finish(struct chef_diskbuilder* builder);

enum chef_partition_attributes {
    CHEF_PARTITION_ATTRIBUTE_BOOT        = 0x1,
    CHEF_PARTITION_ATTRIBUTE_READONLY    = 0x2,
    CHEF_PARTITION_ATTRIBUTE_NOAUTOMOUNT = 0x4,
};

struct chef_disk_partition_params {
    const char*                    name;
    const char*                    uuid;
    unsigned long long             size;
    enum chef_partition_attributes attributes;
};

/**
 * @brief
 */
extern struct chef_disk_partition* chef_diskbuilder_partition_new(struct chef_diskbuilder* builder, struct chef_disk_partition_params* params);

/**
 * @brief
 */
extern int chef_diskbuilder_partition_finish(struct chef_disk_partition* partition);

struct chef_disk_fs_create_directory_params {
    const char* path;
};

struct chef_disk_fs_create_file_params {
    const char* path;
    const void* buffer;
    size_t      size;
};

struct chef_disk_filesystem {
    void (*set_content)(struct chef_disk_filesystem* fs, struct ingredient* ig);
    int (*format)(struct chef_disk_filesystem* fs);
    int (*create_directory)(struct chef_disk_filesystem* fs, struct chef_disk_fs_create_directory_params* params);
    int (*create_file)(struct chef_disk_filesystem* fs, struct chef_disk_fs_create_file_params* params);
    // finish also performs a delete operation on the <fs>
    int (*finish)(struct chef_disk_filesystem* fs);
};

/**
 * @brief FAT32 Filesystem API
 */
extern struct chef_disk_filesystem* chef_filesystem_fat32_new(struct chef_disk_partition* partition);

/**
 * @brief MFS Fileystem API
 */
extern struct chef_disk_filesystem* chef_filesystem_mfs_new(struct chef_disk_partition* partition);

#endif //!__DISKBUILDER_H__
