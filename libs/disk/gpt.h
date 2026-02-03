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

#ifndef __GPT_H__
#define __GPT_H__

#include <stdint.h>

#define __GPT_SIGNATURE "EFI PART"
#define __GPT_REVISION    0x00010000
#define __GPT_HEADER_SIZE 92
#define __GPT_ENTRY_SIZE  128

struct __gpt_header {
    char     signature[8];
    uint32_t revision;
    uint32_t header_size;
    uint32_t header_crc32;
    uint32_t reserved;
    uint64_t main_lba;
    uint64_t backup_lba;
    uint64_t first_usable_lba;
    uint64_t last_usable_lba;
    uint8_t  disk_guid[16];
    uint64_t partition_entry_lba;
    uint32_t partition_entry_count;
    uint32_t partition_entry_size;
    uint32_t partition_array_crc32;
};

#define __GPT_ENTRY_ATTRIB_PLATFORM_REQUIRED    (1ULL << 0)
#define __GPT_ENTRY_ATTRIB_IGNORE               (1ULL << 1)
#define __GPT_ENTRY_ATTRIB_LEGACY_BIOS_BOOTABLE (1ULL << 2)

#define __GPT_ENTRY_ATTRIB_READONLY             (1ULL << 60)
#define __GPT_ENTRY_ATTRIB_SHADOW_COPY          (1ULL << 61)
#define __GPT_ENTRY_ATTRIB_HIDDEN               (1ULL << 62)
#define __GPT_ENTRY_ATTRIB_NO_AUTOMOUNT         (1ULL << 63)


struct __gpt_entry {
    uint8_t  type_guid[16];
    uint8_t  unique_guid[16];
    uint64_t first_sector;
    uint64_t last_sector;
    uint64_t attributes;
    uint16_t name_utf16[36];
};

#endif //!__GPT_H__
