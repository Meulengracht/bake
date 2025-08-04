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

// Known guids
#define GPT_GUID_BIOS_BOOT     "21686148-6449-6E6F-744E-656564454649"
#define GPT_GUID_ESP           "C12A7328-F81F-11D2-BA4B-00A0C93EC93B"
#define GPT_GUID_VALI_SYSTEM   "C4483A10-E3A0-4D3F-B7CC-C04A6E16612B"
#define GPT_GUID_VALI_USERDATA "80C6C62A-B0D6-4FF4-A69D-558AB6FD8B53"
#define GPT_GUID_VALI_USER     "8874F880-E7AD-4EE2-839E-6FFA54F19A72"
#define GPT_GUID_VALI_DATA     "B8E1A523-5865-4651-9548-8A43A9C21384"

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
    const char*                    work_directory;
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
    void (*set_content)(struct chef_disk_filesystem* fs, const char* path);
    int (*format)(struct chef_disk_filesystem* fs);
    // must not fail if directory exists
    int (*create_directory)(struct chef_disk_filesystem* fs, struct chef_disk_fs_create_directory_params* params);
    int (*create_file)(struct chef_disk_filesystem* fs, struct chef_disk_fs_create_file_params* params);
    // finish also performs a delete operation on the <fs>
    int (*finish)(struct chef_disk_filesystem* fs);
};

struct chef_disk_filesystem_params {
    unsigned int sector_size;
};

/**
 * @brief FAT32 Filesystem API
 */
extern struct chef_disk_filesystem* chef_filesystem_fat32_new(struct chef_disk_partition* partition, struct chef_disk_filesystem_params* params);

/**
 * @brief MFS Fileystem API
 */
extern struct chef_disk_filesystem* chef_filesystem_mfs_new(struct chef_disk_partition* partition, struct chef_disk_filesystem_params* params);

#endif //!__DISKBUILDER_H__
